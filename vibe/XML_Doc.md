# XML Document Element Design

## Overview

The Lambda XML processing system uses a virtual `<document>` element as a universal wrapper for all XML content. This design provides consistent internal structure while maintaining clean XML output and enabling proper schema validation.

## Virtual Document Element

The `<document>` element is a **virtual container** that:
- Exists in the parsed Lambda node tree
- Wraps all top-level XML content (elements, processing instructions, comments)
- Is transparent during XML formatting (not output in final XML text)
- Provides a consistent root for schema validation

## Component Behavior

### XML Parser (`input-xml.cpp`)

The XML parser **always** creates a virtual `<document>` wrapper element:

```cpp
// Create a document root element to contain all top-level elements
Element* doc_element = input_create_element(input, "document");

// Parse all top-level content into the document wrapper
while (*xml) {
    Item element = parse_element(input, &xml);
    if (element.item != ITEM_ERROR) {
        list_push((List*)doc_element, element);
        ((TypeElmt*)doc_element->type)->content_length++;
    }
}

// Always return the document wrapper
input->root = {.item = (uint64_t)doc_element};
```

**Key Points:**
- All XML content (declarations, elements, comments) becomes children of `<document>`
- The `<document>` element is always present in the parsed tree
- No special handling for single vs. multiple elements

### XML Formatter (`format-xml.cpp`)

The XML formatter **unwraps** the virtual `<document>` element during output:

```cpp
// Check if this is a "document" element containing multiple children
if (root_type->name.length == 8 && strncmp(root_type->name.str, "document", 8) == 0) {
    List* root_as_list = (List*)root_element;
    
    // Format all children in order (XML declaration, then actual elements)
    for (long i = 0; i < root_as_list->length; i++) {
        Item child = root_as_list->items[i];
        // Format child elements directly without document wrapper
        format_item(sb, child, element_name);
    }
}
```

**Key Points:**
- The `<document>` wrapper itself is never output
- Children of `<document>` are formatted directly
- XML declarations and processing instructions are preserved
- Final output matches original XML structure

### XML Schema Validator

The schema validator works with the **content** of the virtual `<document>` element:

**Design Principle:**
- Schemas define the structure that should match the **children** of `<document>`
- The validator looks inside the `<document>` wrapper to find the actual XML elements
- Schema definitions correspond to the real XML elements, not the virtual wrapper

**Example:**
```xml
<!-- Input XML -->
<?xml version="1.0"?>
<rss version="2.0">
  <channel>...</channel>
</rss>

<!-- Parsed Structure -->
<document>
  <?xml version="1.0"?>
  <rss version="2.0">
    <channel>...</channel>
  </rss>
</document>

<!-- Schema matches the <rss> element inside <document> -->
type Document = <rss version: string; RssChannel>
```

## Benefits

### 1. **Consistent Internal Structure**
- Every XML document has the same root structure in the Lambda tree
- Simplifies processing logic that expects a single root element

### 2. **Preserves XML Declarations**
- Processing instructions like `<?xml version="1.0"?>` are preserved as children
- Multiple top-level elements are properly handled

### 3. **Clean Output**
- Formatted XML matches the original structure
- No artificial wrapper elements in the output

### 4. **Schema Validation Compatibility**
- Schemas can define expected root elements naturally
- Validator works with actual XML content, not wrapper

## Implementation Files

- **Parser**: `lambda/input/input-xml.cpp` - Creates virtual `<document>` wrapper
- **Formatter**: `lambda/format/format-xml.cpp` - Unwraps `<document>` during output
- **Validator**: `lambda/validator/` - Validates content inside `<document>`
- **Tests**: `test/test_input_roundtrip.cpp` - Verifies roundtrip behavior

## Migration Notes

When updating existing code:

1. **Parser Changes**: Always expect `<document>` as root element
2. **Formatter Changes**: Handle `<document>` unwrapping correctly  
3. **Schema Changes**: Define schemas for actual XML elements, not wrapper
4. **Test Changes**: Don't expect `<document>` in formatted output

This design ensures consistent XML processing while maintaining compatibility with standard XML expectations.

## Root Element Patterns Across Lambda Parsers

### Current Parser Root Elements

| **Parser** | **Root Element** | **Pattern** | **Location** |
|------------|------------------|-------------|--------------|
| **XML** | `<document>` | Virtual wrapper (transparent in output) | `input-xml.cpp:734` |
| **Org-mode** | `<org_document>` | Semantic root (appears in output) | `input-org.cpp:1618` |
| **CSS** | `<stylesheet>` | Semantic root (appears in output) | `input-css.cpp:66` |
| **HTML** | *(varies)* | Direct parsing (no wrapper) | `input-html.cpp:745` |
| **Markdown** | *(varies)* | Generic markup parser (no fixed root) | `input-mark.cpp:560` |

### Design Pattern Analysis

**1. Virtual Wrapper Pattern (XML)**
```cpp
// XML creates virtual wrapper that doesn't appear in output
Element* doc_element = input_create_element(input, "document");
// ... parse all content into wrapper ...
input->root = {.item = (uint64_t)doc_element};
```

**2. Semantic Root Pattern (Org, CSS)**
```cpp
// Org-mode creates semantic root that appears in output
Element* doc = create_org_element(input, "org_document");

// CSS creates semantic root that appears in output  
Element* stylesheet = input_create_element(input, "stylesheet");
```

**3. Direct Parsing Pattern (HTML)**
```cpp
// HTML parses actual root element directly
input->root = parse_element(input, &html);
```

**4. Generic Parser Pattern (Markdown)**
```cpp
// Markdown uses generic content parsing
input->root = parse_content(input, &mark);
```

### Unification Opportunities

The XML virtual wrapper approach provides the most consistent internal structure while maintaining clean output. Future unification could standardize on:

- **Virtual document wrapper** for all parsers (internal consistency)
- **Format-specific root elements** preserved as children
- **Transparent formatting** that outputs original structure

This would enable:
- Consistent schema validation patterns
- Uniform processing logic
- Clean roundtrip behavior across all formats
