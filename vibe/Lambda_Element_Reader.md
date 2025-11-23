# Lambda Element Tree Read-Only Interface

## Overview

The Lambda Element Tree Read-Only Interface provides **efficient, const-correct access** to Lambda element trees without exposing the underlying mutable data structures. This interface is designed for safe document processing, template engines, formatters, and tree analysis.

## Key Benefits

### 1. **Memory Safety & Thread Safety**
- **Const-correctness**: All returned pointers are const-qualified
- **No mutation risk**: Interface prevents accidental modification of tree structure
- **Thread-safe reads**: Multiple threads can safely read the same element tree
- **Pool-based allocation**: Efficient memory management with automatic cleanup

### 2. **Performance Optimizations**
- **Cached metadata**: Tag names, counts, and type info cached in readers
- **Zero-copy access**: Direct access to underlying data without copying
- **Efficient iterators**: Optimized depth-first, breadth-first, and filtered traversal
- **Lazy evaluation**: Expensive operations only performed when needed

### 3. **Convenient API**
- **Type-safe access**: Strong typing prevents common errors
- **Rich query methods**: Find by ID, class, attribute, tag name
- **Multiple iteration modes**: Children-only, elements-only, text-only
- **Utility functions**: Text extraction, tree statistics, debug output

## Architecture

```
┌─────────────────────────────────────────────────────────────────┐
│                    Read-Only Interface Layer                    │
├─────────────────────────────────────────────────────────────────┤
│  ElementReader  │  AttributeReader  │  ElementIterator         │
│  - Tag access   │  - Attr queries   │  - Tree traversal        │
│  - Child access │  - Type conversion│  - Multiple modes        │
│  - Text content │  - Safety checks  │  - Depth limiting        │
├─────────────────────────────────────────────────────────────────┤
│                    Lambda Data Structures                      │
│  Element → List → Container    Map → TypeMap → ShapeEntry      │
│  - Reference counting          - Packed data layout             │
│  - Type information           - Efficient field access         │
└─────────────────────────────────────────────────────────────────┘
```

## Core Components

### ElementReader
- **Purpose**: Read-only view of a Lambda Element
- **Caching**: Pre-computes tag name, child count, attribute count
- **Safety**: Validates pointers and provides safe defaults

### AttributeReader  
- **Purpose**: Type-safe attribute access
- **Features**: Existence checks, type conversion, enumeration
- **Performance**: Direct access to packed data structure

### ElementIterator
- **Modes**: 
  - `ITER_CHILDREN_ONLY` - Direct children only
  - `ITER_DEPTH_FIRST` - Full tree, depth-first order
  - `ITER_BREADTH_FIRST` - Full tree, breadth-first order  
  - `ITER_ELEMENTS_ONLY` - Elements only (skip text)
  - `ITER_TEXT_ONLY` - Text nodes only
- **Features**: Depth limiting, resettable, memory efficient

## Usage Examples

### Basic Element Access
```c
// Create reader from element
ElementReader* reader = element_reader_create(element, pool);

// Get element properties
const char* tag = element_reader_tag_name(reader);
int64_t child_count = element_reader_child_count(reader);
bool is_div = element_reader_has_tag(reader, "div");

// Access children
Item child = element_reader_child_at(reader, 0);
String* text = element_reader_text_content(reader, pool);
```

### Attribute Access
```c
// Get attribute reader
AttributeReader* attrs = element_reader_attributes(reader, pool);

// Check and access attributes
if (attribute_reader_has(attrs, "id")) {
    const char* id = attribute_reader_get_cstring(attrs, "id");
    printf("Element ID: %s\n", id);
}
```

### Tree Iteration
```c
// Create iterator for elements only
ElementIterator* iter = element_iterator_create(reader, ITER_ELEMENTS_ONLY, pool);

// Traverse tree
while (element_iterator_has_next(iter)) {
    ElementReader* elem = element_iterator_next_element(iter);
    printf("Found: <%s>\n", element_reader_tag_name(elem));
}
```

### Search Operations
```c
// Find by ID
ElementReader* main_div = element_reader_find_by_id(reader, "main", pool);

// Find by class
ArrayList* highlights = element_reader_find_by_class(reader, "highlight", pool);

// Find by attribute
ArrayList* links = element_reader_find_by_attribute(reader, "href", NULL, pool);
```

### Convenience Macros
```c
// Iterate children with macro
Item child_item;
ELEMENT_READER_FOR_EACH_CHILD(reader, child_item) {
    // Process each child
}

// Elements only
ELEMENT_READER_FOR_EACH_CHILD_ELEMENT(reader, child_element, pool) {
    printf("<%s>\n", element_reader_tag_name(child_element));
ELEMENT_READER_FOR_EACH_END
```

## Integration with Existing Code

### Replace Current Patterns
```c
// OLD: Direct element access (mutable, unsafe)
Element* elem = ...;
TypeElmt* type = (TypeElmt*)elem->type;
List* children = (List*)elem;

// NEW: Read-only interface (safe, efficient)
ElementReader* reader = element_reader_create(elem, pool);
const char* tag = element_reader_tag_name(reader);
int64_t child_count = element_reader_child_count(reader);
```

### Format Function Migration
```c
// OLD: Manual attribute extraction
static String* get_attribute(Element* elem, const char* attr_name) {
    // Complex shape traversal code...
}

// NEW: Simple attribute access
AttributeReader* attrs = element_reader_attributes(reader, pool);
const String* value = attribute_reader_get_string(attrs, attr_name);
```

## Performance Characteristics

| Operation | Time Complexity | Memory | Notes |
|-----------|----------------|---------|-------|
| Create Reader | O(1) | Pool alloc | Caches metadata |
| Tag Access | O(1) | None | Pre-computed |
| Child Access | O(1) | None | Direct array access |
| Attribute Access | O(k) | None | k = # attributes |
| Find Child | O(n) | None | n = # children |
| Tree Traversal | O(n) | O(d) | n = nodes, d = depth |
| Text Extraction | O(n*m) | O(total) | n = nodes, m = avg text |

## Use Cases

### 1. **Document Formatters**
- HTML/XML/Markdown output generation
- Template engine processing
- Content transformation pipelines

### 2. **Analysis Tools**
- Document structure validation
- Content extraction and indexing
- Tree statistics and metrics

### 3. **Interactive Applications**
- DOM-like query operations
- Event handling and binding
- Dynamic content generation

### 4. **Debugging & Diagnostics**
- Tree visualization and inspection
- Structure validation
- Performance profiling

## Future Enhancements

### 1. **Advanced Queries**
- CSS selector support
- XPath-like expressions
- Regular expression matching

### 2. **Performance Optimizations**
- Index-based lookups for large trees
- Lazy attribute parsing
- Memory-mapped tree access

### 3. **Additional Iterators**
- Filtered iterators with predicates
- Parallel iteration support
- Streaming interfaces for large documents

## Conclusion

The Lambda Element Tree Read-Only Interface provides a **safe, efficient, and convenient** way to access Lambda element trees. It significantly reduces the complexity and error-proneness of tree navigation while maintaining excellent performance characteristics.

This interface is particularly valuable for:
- **Document processing pipelines** that need safe, repeatable access
- **Multi-threaded applications** requiring concurrent read access  
- **Complex tree analysis** that benefits from rich query capabilities
- **Performance-critical code** that needs zero-copy access patterns