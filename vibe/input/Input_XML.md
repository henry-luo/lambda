# XML Document Element Design

## Overview

The Lambda XML processing system uses a virtual `<document>` element as a universal wrapper for all XML content. This design provides consistent internal structure while maintaining clean XML output and enabling proper schema validation.

## Virtual Document Element

The `<document>` element is a **virtual container** that:
- Exists in the parsed Lambda node tree
- Wraps all top-level XML content (elements, processing instructions, comments)
- Is transparent during XML formatting (not output in final XML text)
- Provides a consistent root for schema validation

## XHTML admission

**Adopted 2026-10-15; catalog revision 2026-10-26 — D2.10.1v2**, with name identity governed by
**S8.2.2v6** and **D3.4.4v4**. The dedicated `xhtml` format reuses strict XML
parsing and emits the HTML parser's tree representation for direct Radiant
consumption. `.xhtml`/`.xht` and `application/xhtml+xml` select it; an explicit
format overrides the filename.

The XML parser collects an element's ordered attributes before constructing
its output element. This makes every namespace declaration on the start tag
available when selecting the element's emitted name. XHTML, SVG and MathML
names use their exact local spelling; the source QName remains available for
matching closing tags. Qualified attributes and namespace declarations remain
intact. Existing DOM namespace metadata carries resolved element identity,
including default-namespace resets and XHTML inside SVG `foreignObject`.
All names are created through `MarkBuilder`, sharing the HTML parser's
interning path without mutating shared element types (D3.4.3v5).

XHTML produces `#document`, `#doctype` and `#comment` nodes and omits the XML
declaration from document children. Text whitespace and decoded CDATA survive.
It neither applies HTML tree repair nor serializes and reparses the tree.
The root must be XHTML `html`; malformed XML, unbound prefixes, duplicate
expanded attributes and embedded NUL bytes fail parsing. External doctype
identifiers are retained without fetching their DTDs. In the XHTML profile,
the recognized public identifiers in
[HTML §14.2](https://html.spec.whatwg.org/multipage/xhtml.html#parsing-xhtml-documents)
select the existing full HTML named-character-reference catalog. This includes
multi-codepoint and supplementary-plane replacements in text and attributes;
it is a public-ID catalog lookup, not DTD execution or source rewriting.
Unknown public identifiers and documents without a matching public identifier
keep XML's five predefined entities. Internal subsets, arbitrary declarations,
unterminated references and unknown entity names remain errors. This extension
does not broaden ordinary XML parsing profiles.

Radiant selects this profile from the local extension or final HTTP response
MIME and shares the existing HTML loader after parsing. XHTML stays in
standards mode without a doctype, and its DOM retains XML name sensitivity.
The Lambda script loader also unwraps parsed `#document` results before CSS
discovery, so a script returning `input(path, 'xhtml')^` preserves its head.
The ordinary `xml` profile continues to return `document` and keep source QNames.

Regression coverage: `HtmlParserTest.Xhtml*`, `test/lambda/input_xhtml.ls`
and its golden, paired `test/html/xhtml_names.{html,xhtml}` with shared CSS
and event assertions, and the MIME detector's input mappings.

The viewable `test/html/xhtml_feature_gallery.xhtml` expands this into ten
sections covering entities/UTF-8, CDATA, whitespace, self-closing elements,
default/scoped/custom/reset namespaces, qualified and case-sensitive names,
external/inline CSS, SVG/XLink/foreignObject, MathML, tables/lists, forms and
local assets. Its external script displays 27 DOM/style checks and exposes
their failures without hiding them; checks begin after page load and the
first animation frame. `RadiantViewTest.LoadsXhtmlFeatureGallery` verifies
the checklist and click/rerun behavior. The gallery also exposed and covers
the type selector's XML document check: the XHTML namespace alone must not
enable HTML case-insensitive matching (D2.10.1, S8.2.2v6).

The gallery's 27 checks and nine replay assertions pass in interactive views
at 1120 px and 480 px widths. Static layout/render currently exposes a
separate CSSOM startup gap: the uppercase element's computed border width
can still read `0px` before its view geometry is committed, although its
selector matches and its final border paints correctly. The gallery keeps
that failed check visible with the observed values; it does not suppress it.

## Component Behavior

### XML Parser (`input-xml.cpp`)

The ordinary `xml` profile creates a virtual `<document>` wrapper element;
the `xhtml` profile uses `#document` under D2.10.1:

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
