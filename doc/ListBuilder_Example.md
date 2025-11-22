# ListBuilder Usage Example

The `ListBuilder` class provides a fluent API for constructing List structures in the Lambda runtime. Lists have special semantics compared to Arrays:

## Key Differences: List vs Array

| Feature | List (`list_push`) | Array (`array_append`) |
|---------|-------------------|----------------------|
| Null values | **Skipped** | **Preserved** |
| Nested structures | **Flattened** | **Preserved** |
| Use case | Text fragments, flattening | Generic collections |

## Basic Usage

```cpp
#include "lambda/mark_builder.hpp"

void example_list_builder(Input* input) {
    MarkBuilder builder(input);
    
    // Create a simple list
    Item list = builder.list()
        .push(builder.createInt(1))
        .push(builder.createInt(2))
        .push(builder.createInt(3))
        .final();
    
    // list now contains: [1, 2, 3]
}
```

## Null Skipping Behavior

```cpp
void example_null_skipping(Input* input) {
    MarkBuilder builder(input);
    
    Item list = builder.list()
        .push(builder.createInt(1))
        .push(builder.createNull())      // Skipped!
        .push(builder.createInt(2))
        .push(builder.createNull())      // Skipped!
        .push(builder.createInt(3))
        .final();
    
    List* lst = list.list;
    // lst->length == 3 (nulls were skipped)
    // Contents: [1, 2, 3]
}
```

## List Flattening Behavior

```cpp
void example_list_flattening(Input* input) {
    MarkBuilder builder(input);
    
    // Create inner list
    Item inner = builder.list()
        .push(builder.createInt(2))
        .push(builder.createInt(3))
        .final();
    
    // Create outer list with nested list
    Item outer = builder.list()
        .push(builder.createInt(1))
        .push(inner)                     // Flattened!
        .push(builder.createInt(4))
        .final();
    
    List* lst = outer.list;
    // lst->length == 4 (inner list was flattened)
    // Contents: [1, 2, 3, 4]
}
```

## Convenience Methods

```cpp
void example_convenience_methods(Input* input) {
    MarkBuilder builder(input);
    
    Item list = builder.list()
        .push("Hello")           // String
        .push(42)                // int64_t
        .push(3.14)              // double
        .push(true)              // bool
        .final();
    
    // Mixed-type list with automatic type conversion
}
```

## Initializer List

```cpp
void example_initializer_list(Input* input) {
    MarkBuilder builder(input);
    
    Item list = builder.list()
        .pushItems({
            builder.createInt(1),
            builder.createInt(2),
            builder.createInt(3)
        })
        .final();
    
    // Convenient for multiple items at once
}
```

## Memory Management

ListBuilder uses **arena allocation** for the List structure:

- **Stack-allocated builder**: `ListBuilder` itself is a temporary stack object
- **Arena-allocated data**: The `List*` is allocated from `Input->arena`
- **Automatic cleanup**: Builder destroyed when scope ends, List survives in arena
- **No manual free**: Arena manages all memory, freed when Input is destroyed

```cpp
void parse_document(Input* input) {
    MarkBuilder builder(input);          // Stack allocation
    
    Item result = builder.list()         // Arena allocation
        .push(builder.createInt(1))
        .push(builder.createInt(2))
        .final();
    
    input->root = result;
}  // builder destroyed here, but List survives in arena
```

## When to Use List vs Array

**Use List when:**
- Building text fragments that should merge
- Flattening nested structures
- Skipping null values automatically
- Processing Lambda script list literals

**Use Array when:**
- Need to preserve nulls (e.g., sparse arrays)
- Need to preserve nested structure (no flattening)
- Generic collection with all values significant
- JSON array preservation

## API Reference

### ListBuilder Methods

```cpp
class ListBuilder {
    ListBuilder& push(Item item);            // Push item (skips null, flattens lists)
    ListBuilder& push(const char* str);      // Push string
    ListBuilder& push(int64_t value);        // Push integer
    ListBuilder& push(double value);         // Push float
    ListBuilder& push(bool value);           // Push boolean
    ListBuilder& pushItems(std::initializer_list<Item> items);  // Push multiple
    Item final();                             // Finalize and return List
};
```

### MarkBuilder List Methods

```cpp
class MarkBuilder {
    ListBuilder list();                      // Create ListBuilder
    Item createList();                       // Create empty List directly
};
```

## Implementation Notes

- Uses `list_arena()` for arena allocation
- Calls `list_push()` internally (has special semantics)
- List length grows dynamically with `expand_list()`
- Arena-allocated structure lives until `arena_reset()` or `arena_destroy()`

## Test Coverage

See `test/test_mark_builder_gtest.cpp`:
- `CreateList` - Basic list creation
- `CreateEmptyList` - Empty list handling
- `ListSkipsNulls` - Null skipping behavior
- `ListFlattensNestedLists` - Nested list flattening

All tests passing ✅ (69/69 MarkBuilder tests)
