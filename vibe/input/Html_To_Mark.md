# HTML to Lambda/Mark Tree Mapping

This document describes how HTML documents are parsed and mapped to Lambda's internal Mark tree data structures.

## Overview

The HTML parser (`lambda/input/input-html.cpp`) converts HTML documents into Lambda's Element-based tree structure. This process involves:

1. **Tokenization**: Using Lexbor HTML parser to tokenize HTML
2. **Tree Construction**: Building Lambda Element nodes with proper parent-child relationships
3. **Attribute Handling**: Converting HTML attributes to Element properties
4. **Special Node Handling**: Managing DOCTYPE, comments, XML declarations, and other non-element nodes

## Root Level Structure

### Parsed Output Format

When an HTML document is parsed, the root structure depends on the document content:

```lambda
// Simple HTML without DOCTYPE
<div>content</div>
→ Element(tag: "div", children: [...])

// Full HTML with DOCTYPE
<!DOCTYPE html>
<html>...</html>
→ List[
    Element(tag: "!DOCTYPE", attrs: {html: null}),
    Element(tag: "html", children: [...])
]

// HTML with comments
<!DOCTYPE html>
<!-- Comment -->
<html>...</html>
→ List[
    Element(tag: "!DOCTYPE", ...),
    Element(tag: "!--", text: " Comment "),
    Element(tag: "html", ...)
]
```

### Root Element Extraction

To get the actual document root (skipping DOCTYPE and comments):

```cpp
Element* get_root_element(Input* input) {
    void* root_ptr = (void*)input->root.pointer;
    List* potential_list = (List*)root_ptr;

    if (potential_list->type_id == LMD_TYPE_LIST) {
        // Search for first real element (skip DOCTYPE, comments)
        for (int64_t i = 0; i < potential_list->length; i++) {
            Item item = potential_list->items[i];
            Element* elem = get_element_from_item(item);

            if (elem) {
                TypeElmt* type = (TypeElmt*)elem->type;
                const char* tag = type->name.str;

                // Skip DOCTYPE and comments
                if (strcmp(tag, "!DOCTYPE") != 0 &&
                    strcmp(tag, "!--") != 0) {
                    return elem;  // Found actual root (html, body, div, etc.)
                }
            }
        }
    } else if (potential_list->type_id == LMD_TYPE_ELEMENT) {
        return (Element*)root_ptr;  // Direct element
    }

    return nullptr;
}
```

## Special Node Types

### 1. DOCTYPE Declaration

**HTML Input:**
```html
<!DOCTYPE html>
<!DOCTYPE html PUBLIC "-//W3C//DTD XHTML 1.0 Strict//EN" "...">
```

**Lambda Representation:**
```lambda
Element(
    tag: "!DOCTYPE",
    attributes: {
        html: null,           // For simple DOCTYPE
        PUBLIC: "...",        // For public DOCTYPE
        ...
    }
)
```

**Key Points:**
- Tag name is `"!DOCTYPE"`
- Type information stored as attributes
- Not a container element (no children)
- Should be skipped when traversing document tree

### 2. XML Declaration

**HTML Input:**
```html
<?xml version="1.0" encoding="UTF-8"?>
```

**Lambda Representation:**
```lambda
Element(
    tag: "?xml",
    attributes: {
        version: "1.0",
        encoding: "UTF-8"
    }
)
```

**Key Points:**
- Tag name starts with `"?"`
- Processing instruction attributes preserved
- No children

### 3. Comments

**HTML Input:**
```html
<!-- This is a comment -->
<!-- Multi-line
     comment content -->
```

**Lambda Representation:**
```lambda
Element(
    tag: "!--",
    children: [String(" This is a comment ")]
)
```

**Key Points:**
- Tag name is `"!--"`
- Comment text stored as first child (String type)
- Leading/trailing spaces preserved
- No attributes

### 4. CDATA Sections

**HTML Input:**
```html
<![CDATA[
    raw < & > text
]]>
```

**Lambda Representation:**
```lambda
Element(
    tag: "![CDATA[",
    children: [String("\n    raw < & > text\n")]
)
```

**Key Points:**
- Tag name is `"![CDATA["`
- Content stored as raw string (no entity decoding)
- Whitespace preserved

## Attribute Handling

### Empty String Attributes

HTML allows empty attribute values, which Lambda handles specially:

**HTML Input:**
```html
<div class="" id="test"></div>
<input type="" name="field">
```

**Lambda Storage:**
```cpp
// Empty string "" is stored as NULL pointer with LMD_TYPE_NULL type
Element {
    shape_entry: {
        name: "class",
        type: LMD_TYPE_NULL,    // ← NULL string indicator
        value: NULL              // ← NULL pointer
    },
    shape_entry: {
        name: "id",
        type: LMD_TYPE_STRING,
        value: String("test")
    }
}
```

**Implementation Details:**

```cpp
// Parser: parse_attribute_value() in input-html.cpp
const char* parse_attribute_value(...) {
    if (*ptr == '"' || *ptr == '\'') {
        char quote = *ptr++;
        const char* start = ptr;

        while (*ptr && *ptr != quote) ptr++;

        if (ptr == start) {
            // Empty quoted string: class=""
            return NULL;  // ← Returns NULL for empty strings
        }

        return pool_strndup(pool, start, ptr - start);
    }
    return NULL;
}

// Storage: elmt_put() in input.cpp
void elmt_put(Element* elem, const char* key, void* value) {
    // ...
    if (entry->type->type_id == LMD_TYPE_STRING) {
        String* old_str = *(String**)field_ptr;
        if (old_str) old_str->ref_cnt--;

        String* new_str = (String*)value;
        if (new_str) new_str->ref_cnt++;  // ← NULL check prevents crash

        *(String**)field_ptr = new_str;
    }
    // NULL values automatically get LMD_TYPE_NULL via get_type_id(NULL)
}

// Formatter: format_html() in format-html.cpp
void format_html_element(...) {
    ShapeEntry* entry = elem_type->shape;
    while (entry) {
        const char* attr_name = entry->name->str;
        void* field_ptr = (char*)elem->data + entry->byte_offset;

        // Check for both STRING and NULL types
        if (entry->type->type_id == LMD_TYPE_STRING ||
            entry->type->type_id == LMD_TYPE_NULL) {

            String* value = *(String**)field_ptr;

            fprintf(out, " %s=\"", attr_name);
            if (value) {
                fprintf(out, "%s", value->chars);  // Non-empty string
            }
            // Empty: outputs attribute=""
            fprintf(out, "\"");
        }
        entry = entry->next;
    }
}
```

**Roundtrip Behavior:**
```html
Input:  <div class="" id="test"></div>
Parse:  class → NULL (LMD_TYPE_NULL), id → "test" (LMD_TYPE_STRING)
Output: <div class="" id="test"></div>  ✓ Preserves empty attributes
```

### Boolean Attributes

HTML boolean attributes can appear without values:

**HTML Input:**
```html
<input disabled>
<input checked="checked">
<button disabled="">
```

**Lambda Representation:**

Boolean attributes **are fully supported** by the parser:

```html
<input disabled>          → disabled: true (LMD_TYPE_BOOL)
<input disabled="">       → disabled: NULL (LMD_TYPE_NULL, empty string)
<input disabled="true">   → disabled: "true" (LMD_TYPE_STRING)
<input checked="checked"> → checked: "checked" (LMD_TYPE_STRING)
```

**Implementation Details:**

```cpp
// From parse_attributes() in input-html.cpp (lines 413-432)
Item attr_value;
if (**html == '=') {
    (*html)++; // Skip =
    skip_whitespace(html); // Skip whitespace after =
    String* str_value = parse_attribute_value(input, html, html_start);
    // Store attribute value (NULL for empty strings like class="")
    attr_value = (Item){.item = s2it(str_value)};
    // Type will be LMD_TYPE_NULL if str_value is NULL, LMD_TYPE_STRING otherwise
} else {
    // Boolean attribute (no value) - store as boolean true
    attr_value = (Item){.bool_val = true};
    attr_value.type_id = LMD_TYPE_BOOL;
}

// Add attribute to element (including NULL values for empty attributes)
elmt_put(element, attr_name, attr_value, input->pool);
```

**Type Distinction:**

The parser distinguishes three cases:

| HTML Syntax | Lambda Type | Value | Example |
|-------------|-------------|-------|---------|
| `disabled` | `LMD_TYPE_BOOL` | `true` | `<input disabled>` |
| `disabled=""` | `LMD_TYPE_NULL` | `NULL` | `<input disabled="">` |
| `disabled="value"` | `LMD_TYPE_STRING` | `"value"` | `<input disabled="disabled">` |

**Roundtrip Formatting:**

The HTML formatter correctly handles all three cases:

```cpp
// From format_html() in format-html.cpp (lines 451-467)
if (field_type == LMD_TYPE_BOOL) {
    // Boolean attribute (bool value) - output name only
    stringbuf_append_char(sb, ' ');
    stringbuf_append_format(sb, "%.*s", field_name_len, field_name);
} else if (field_type == LMD_TYPE_STRING || field_type == LMD_TYPE_NULL) {
    // String attribute or NULL (empty string like class="")
    // Always output as attribute="value" format
    String* str = *(String**)data;
    stringbuf_append_char(sb, ' ');
    stringbuf_append_format(sb, "%.*s=\"", field_name_len, field_name);
    if (str && str->chars) {
        format_html_string(sb, str, true);  // true = is_attribute
    }
    // Always close the quote, even for NULL/empty strings
    stringbuf_append_char(sb, '"');
}
```

**Formatting Output:**

| Lambda Type | Lambda Value | HTML Output |
|-------------|--------------|-------------|
| `LMD_TYPE_BOOL` | `true` | `disabled` (no value) |
| `LMD_TYPE_NULL` | `NULL` | `disabled=""` |
| `LMD_TYPE_STRING` | `"disabled"` | `disabled="disabled"` |

**Complete Roundtrip Example:**

```html
<!-- Input -->
<input disabled checked="" type="text">

<!-- Parse -->
disabled: true (LMD_TYPE_BOOL)
checked: NULL (LMD_TYPE_NULL)
type: "text" (LMD_TYPE_STRING)

<!-- Output -->
<input disabled checked="" type="text">  ✓ Perfect roundtrip
```### Attribute Value Escaping

HTML entities in attribute values are decoded during parsing:

**HTML Input:**
```html
<div title="&lt;tag&gt; &amp; &quot;quotes&quot;"></div>
<a href="?foo=1&amp;bar=2"></a>
```

**Lambda Storage:**
```lambda
Element(tag: "div", title: "<tag> & \"quotes\"")  // Decoded
Element(tag: "a", href: "?foo=1&bar=2")           // Decoded
```

**Output Encoding:**

When formatting back to HTML, special characters are re-encoded:

```cpp
void write_escaped_attr(const char* text) {
    for (const char* p = text; *p; p++) {
        switch (*p) {
            case '<':  fprintf(out, "&lt;");   break;
            case '>':  fprintf(out, "&gt;");   break;
            case '&':  fprintf(out, "&amp;");  break;
            case '"':  fprintf(out, "&quot;"); break;
            case '\'': fprintf(out, "&apos;"); break;  // XML-style
            default:   fputc(*p, out);         break;
        }
    }
}
```

## Element Tree Structure

### Parent-Child Relationships

Lambda Elements store children as a List:

**HTML Input:**
```html
<div id="parent">
    <p>Paragraph 1</p>
    Text node
    <p>Paragraph 2</p>
</div>
```

**Lambda Structure:**
```lambda
Element {
    type: TypeElmt(name: "div"),
    data: {id: "parent"},
    length: 3,              // Inherited from List
    items: [
        Item(type: ELEMENT, pointer: Element(tag: "p")),
        Item(type: STRING, pointer: String("Text node")),
        Item(type: ELEMENT, pointer: Element(tag: "p"))
    ]
}
```

**Access Pattern:**
```cpp
Element* elem = ...;
List* list = (List*)elem;  // Element inherits from List

for (int64_t i = 0; i < list->length; i++) {
    Item child = list->items[i];

    if (child.type_id == LMD_TYPE_ELEMENT) {
        Element* child_elem = (Element*)child.pointer;
        // Process child element
    } else if (child.type_id == LMD_TYPE_STRING) {
        String* text = (String*)child.pointer;
        // Process text node
    }
}
```

### Text Node Handling

Text content is stored as String items:

**HTML Input:**
```html
<p>Simple text</p>
<div>Text with <em>emphasis</em> inline</div>
```

**Lambda Structure:**
```lambda
// Simple text
Element(tag: "p", children: [
    String("Simple text")
])

// Mixed content
Element(tag: "div", children: [
    String("Text with "),
    Element(tag: "em", children: [String("emphasis")]),
    String(" inline")
])
```

**Whitespace Handling:**

The parser preserves whitespace as-is (no normalization):

```html
<p>  Multiple   spaces  </p>
→ String("  Multiple   spaces  ")

<div>
    Indented text
</div>
→ String("\n    Indented text\n")
```

## Type System Integration

### Element Type Representation

Each unique tag name gets a `TypeElmt` instance:

```cpp
typedef struct {
    Type base;              // type_id = LMD_TYPE_ELEMENT
    Name name;              // Tag name (e.g., "div", "html")
    ShapeEntry* shape;      // Linked list of attributes
    // ... other fields
} TypeElmt;
```

**Shape Entries for Attributes:**

```cpp
typedef struct ShapeEntry {
    Name* name;             // Attribute name
    Type* type;             // Value type (STRING, NULL, etc.)
    int byte_offset;        // Offset in elem->data
    ShapeEntry* next;       // Next attribute
} ShapeEntry;
```

**Example:**

```html
<div id="main" class="container" data-value="42">
```

```cpp
TypeElmt {
    name: "div",
    shape: [
        {name: "id", type: STRING, offset: 0} →
        {name: "class", type: STRING, offset: 8} →
        {name: "data-value", type: STRING, offset: 16} →
        NULL
    ]
}
```

### Type ID Mapping

Lambda's type system uses high-byte type IDs:

```cpp
#define LMD_TYPE_NULL        0x00
#define LMD_TYPE_ELEMENT     0x13  // 19
#define LMD_TYPE_STRING      0x08  // 8
#define LMD_TYPE_LIST        0x0C  // 12
#define LMD_TYPE_RAW_POINTER 0xFF  // 255

// Type retrieval
uint8_t get_type_id(void* ptr) {
    if (!ptr) return LMD_TYPE_NULL;

    // Check high byte of pointer for type tag
    uint8_t* type_ptr = (uint8_t*)ptr;
    return type_ptr[offsetof(Item, type_id)];
}
```

## Parsing Pipeline

### 1. Input Stage

```cpp
// Entry point: input_from_source() in input.cpp
Input* input_from_source(
    const char* content,
    Url* source,
    String* type,      // "html"
    String* flavor     // NULL or "html5", "xhtml"
) {
    if (strcmp(type->chars, "html") == 0) {
        return parse_html(content, source, flavor);
    }
    // ... other formats
}
```

### 2. HTML Parsing

```cpp
// Lexbor integration: parse_html() in input-html.cpp
Input* parse_html(const char* content, Url* source, String* flavor) {
    // 1. Initialize Lexbor parser
    lxb_html_document_t* document = lxb_html_document_create();
    lxb_html_document_parse(document, (const lxb_char_t*)content, length);

    // 2. Get root node
    lxb_dom_node_t* root = lxb_dom_interface_node(document);

    // 3. Convert to Lambda structure
    void* lambda_root = convert_lexbor_node(root, pool);

    // 4. Create Input wrapper
    Input* input = input_create(lambda_root, source, type, flavor);

    // 5. Cleanup Lexbor document
    lxb_html_document_destroy(document);

    return input;
}
```

### 3. Node Conversion

```cpp
// Recursive conversion: convert_lexbor_node()
void* convert_lexbor_node(lxb_dom_node_t* node, Pool* pool) {
    switch (node->type) {
        case LXB_DOM_NODE_TYPE_ELEMENT: {
            lxb_dom_element_t* elem = lxb_dom_interface_element(node);

            // Create Lambda Element
            const char* tag = (const char*)lxb_dom_element_local_name(elem, NULL);
            Element* lambda_elem = input_create_element(tag);

            // Parse attributes
            parse_attributes(lambda_elem, elem);

            // Parse children recursively
            lxb_dom_node_t* child = lxb_dom_node_first_child(node);
            while (child) {
                void* lambda_child = convert_lexbor_node(child, pool);
                if (lambda_child) {
                    list_push((List*)lambda_elem, create_item(lambda_child));
                }
                child = lxb_dom_node_next(child);
            }

            return lambda_elem;
        }

        case LXB_DOM_NODE_TYPE_TEXT: {
            lxb_dom_text_t* text = lxb_dom_interface_text(node);
            size_t len;
            const char* data = (const char*)lxb_dom_node_text_content(node, &len);
            return create_string(data, len);
        }

        case LXB_DOM_NODE_TYPE_COMMENT: {
            // Create comment element: <Element tag="!--">
            Element* comment = input_create_element("!--");
            // Store comment text as child
            const char* text = get_comment_text(node);
            list_push((List*)comment, create_item(create_string(text)));
            return comment;
        }

        case LXB_DOM_NODE_TYPE_DOCUMENT_TYPE: {
            // Create DOCTYPE element: <Element tag="!DOCTYPE">
            lxb_dom_document_type_t* doctype = lxb_dom_interface_document_type(node);
            Element* dt = input_create_element("!DOCTYPE");

            // Store DOCTYPE attributes
            const char* name = get_doctype_name(doctype);
            if (name) elmt_put(dt, name, NULL);

            return dt;
        }

        default:
            return NULL;  // Skip unsupported node types
    }
}
```

## Output Formatting

### HTML Generation

```cpp
// Format Element back to HTML: format_html() in format-html.cpp
void format_html(Element* elem, FILE* out, int indent) {
    TypeElmt* type = (TypeElmt*)elem->type;
    const char* tag = type->name.str;

    // Special handling for non-standard elements
    if (strcmp(tag, "!DOCTYPE") == 0) {
        fprintf(out, "<!DOCTYPE");
        format_doctype_attrs(elem, out);
        fprintf(out, ">");
        return;
    }

    if (strcmp(tag, "!--") == 0) {
        fprintf(out, "<!--");
        format_comment_text(elem, out);
        fprintf(out, "-->");
        return;
    }

    // Regular element
    fprintf(out, "<%s", tag);

    // Attributes
    format_attributes(elem, out);

    // Children
    List* list = (List*)elem;
    if (list->length == 0 && is_void_element(tag)) {
        fprintf(out, ">");  // Void element: <img>, <br>, etc.
    } else {
        fprintf(out, ">");

        for (int64_t i = 0; i < list->length; i++) {
            Item child = list->items[i];
            format_item(child, out, indent + 1);
        }

        fprintf(out, "</%s>", tag);
    }
}
```

## Roundtrip Guarantees

### Preserved Elements

✅ **Preserved:**
- Element structure and nesting
- Tag names (case-sensitive)
- Attribute names and values
- Empty string attributes (`class=""`)
- Text content and whitespace
- Comment text
- DOCTYPE declarations

### Known Transformations

⚠️ **Transformed:**
- Boolean attributes without values → Lost (not currently captured)
- Entity references → Decoded to Unicode characters
- Attribute order → May change (shapeless storage order)
- Whitespace between attributes → Normalized to single space
- Self-closing tag style → Normalized (`<br/>` → `<br>`)

### Example Roundtrip

```html
<!-- Input -->
<!DOCTYPE html>
<html lang="en">
  <head>
    <meta charset="UTF-8">
    <title>Test &amp; Example</title>
  </head>
  <body class="">
    <!-- Comment -->
    <div id="main" data-value="42">
      <p>Text with &lt;entities&gt;</p>
    </div>
  </body>
</html>

<!-- After Parse + Format (semantically identical) -->
<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="UTF-8">
<title>Test & Example</title>
</head>
<body class="">
<!-- Comment -->
<div id="main" data-value="42">
<p>Text with <entities></p>
</div>
</body>
</html>
```

## Testing

### Roundtrip Tests

The test suite (`test/test_html_roundtrip_gtest.cpp`) validates HTML parsing and formatting:

```cpp
TEST(HtmlRoundtrip, EmptyAttributes) {
    const char* html = "<div class=\"\" id=\"test\"></div>";

    // Parse
    Input* input = parse_html_string(html);
    Element* root = get_root_element(input);

    // Format back
    char* output = format_to_string(root, "html");

    // Compare (should preserve empty class="")
    EXPECT_STREQ(output, html);
}

TEST(HtmlRoundtrip, DOCTYPE) {
    const char* html = "<!DOCTYPE html><html></html>";

    Input* input = parse_html_string(html);

    // Root should be a List with [DOCTYPE, html]
    List* root_list = (List*)input->root.pointer;
    EXPECT_EQ(root_list->type_id, LMD_TYPE_LIST);
    EXPECT_EQ(root_list->length, 2);

    // First element should be DOCTYPE
    Element* doctype = (Element*)root_list->items[0].pointer;
    EXPECT_STREQ(get_tag_name(doctype), "!DOCTYPE");
}
```

### Integration Tests

CSS integration tests (`test/test_html_css_gtest.cpp`) validate DOM conversion:

```cpp
TEST_F(HtmlCssIntegrationTest, RootElementExtraction) {
    const char* html = "<!DOCTYPE html><html><head></head><body></body></html>";

    Input* input = parse_html_string(html);
    Element* root = get_root_element(input);  // Should skip DOCTYPE

    EXPECT_STREQ(get_tag_name(root), "html");  // Not "!DOCTYPE"

    // HTML should have children
    int child_count = dom_element_count_child_elements(
        lambda_element_to_dom_element(root, pool)
    );
    EXPECT_EQ(child_count, 2);  // <head> and <body>
}
```

## Future Enhancements

### 1. Boolean Attribute Support

Add proper handling for boolean attributes:

```cpp
// Capture: <input disabled> → disabled: true
// Format: disabled: true → <input disabled>
```

### 2. Namespace Support

Handle XML namespaces properly:

```html
<svg xmlns="http://www.w3.org/2000/svg">
  <rect width="100" height="100"/>
</svg>
```

### 3. Custom Element Support

Support web components and custom elements:

```html
<custom-element data-prop="value">
  <template>...</template>
</custom-element>
```

### 4. Source Location Tracking

Preserve line/column information for error reporting:

```cpp
typedef struct {
    Element base;
    int line;
    int column;
    Url* source;
} ElementWithLocation;
```

## See Also

- **Lambda Data Structures**: `lambda/lambda-data.hpp`
- **HTML Parser**: `lambda/input/input-html.cpp`
- **HTML Formatter**: `lambda/format/format-html.cpp`
- **Roundtrip Tests**: `test/test_html_roundtrip_gtest.cpp`
- **CSS Integration**: `doc/CSS_Integration.md` (if exists)
- **Type System**: `lambda/Lamdba_Runtime.md`
