# Lambda Runtime Data Management

### Lambda Data Structures
Lambda runtime uses the following design/convention to represent and manage its runtime data:
- for simple scalar types: LMD_TYPE_NULL, LMD_TYPE_BOOL, LMD_TYPE_INT
	- they are packed into Item, with high bits set to TypeId;
- for compound scalar types: LMD_TYPE_INT64, LMD_TYPE_FLOAT, LMD_TYPE_DTIME, LMD_TYPE_DECIMAL, LMD_TYPE_SYMBOL, LMD_TYPE_STRING, LMD_TYPE_BINARY
	- they are packed into item as a tagged pointer. It's a pointer to the actual data, with high bits set to TypeId.
	- LMD_TYPE_INT64, LMD_TYPE_FLOAT, LMD_TYPE_DTIME are stored in a special num_stack at runtime;
	- LMD_TYPE_DECIMAL, LMD_TYPE_SYMBOL, LMD_TYPE_STRING, LMD_TYPE_BINARY are allocated from heap, and reference counted;
- for container types: LMD_TYPE_LIST, LMD_TYPE_RANGE, LMD_TYPE_ARRAY_INT, LMD_TYPE_ARRAY, LMD_TYPE_MAP, LMD_TYPE_ELEMENT
	- they are direct pointers to the container data.
	- all containers extends struct Container, that starts with field TypeId;
	- they are heap allocated, and reference counted;
- Lambda map/LMD_TYPE_MAP, uses a packed struct:
	- its list of fields are defined as a linked list of ShapeEntry;
	- and the actual data are stored as a packed struct;
- Lambda element/LMD_TYPE_ELEMENT, extends Lambda list/LMD_TYPE_LIST, and it's also a map/LMD_TYPE_MAP at the same time;
	- note that it can be casted as List directly, but not Map directly;
- can use get_type_id() function to get the TypeId of an Item in a general manner;

### Header Files
Lambda header files defined the runtime data. They are layer one up on the other, from basic data structs, to the full runtime transpiler and runner definition.
- *lambda.h*: 
	- the fundamental data structures of Lambda;
	- the C version is for MIR JIT compiler; 
		- thus it defines the API of Lambda runtime that is exposed to C2MIR JIT compiler;
	- the C++ version is for the manual-written/AOT-compiled Lambda runtime code;
- *lambda-data.hpp*:
	- the full C++ definitions of the data structures and the API functions to work with the data;
	- input parsers work at this level;
- *ast.hpp*:
	- the AST built from Tree-sitter syntax tree;
	- Lambda validator, formatter works at this level;
- *transpiler.hpp*:
	- the full Lambda transpiler and code runner;
### String Memory Management

Lambda uses three distinct string allocation strategies optimized for different use cases:

#### 1. Names (Structural Identifiers)
**Function**: `heap_create_name(const char* str, size_t len)`
**Pooling**: Always pooled in NamePool (string interning)
**Use Cases**:
- Map keys
- Element tag names  
- Element attribute names
- Function names
- Variable names
- Any structural identifier that appears multiple times

**Benefits**: 
- Same name string always returns same pointer (identity comparison)
- Memory sharing across entire document hierarchy
- Inherits from parent NamePool (schemas share names with instances)

**Example**:
```cpp
// Map keys are structural names
map.put("name", value);      // "name" is pooled
map.put("age", value);       // "age" is pooled
map.put("name", value2);     // Reuses same "name" pointer

// Element construction
Element* div = builder.createElement("div");  // "div" is pooled
div->attr("class", "container");             // "class" is pooled
```

#### 2. Symbols (Short Identifiers)  
**Function**: `heap_create_symbol(const char* str, size_t len)`
**Pooling**: Conditionally pooled (only if length ≤ 32 chars)
**Use Cases**:
- Symbol literals in Lambda code: `'mySymbol`
- Short identifier strings
- Enum-like values

**Benefits**:
- Common short symbols are pooled (memory sharing)
- Long symbols fall back to arena allocation (no overhead)

**Size Limit**: `NAME_POOL_SYMBOL_LIMIT = 32` characters

**Example**:
```cpp
// Short symbols are pooled
Item sym1 = heap_create_symbol("ok", 2);      // Pooled
Item sym2 = heap_create_symbol("error", 5);   // Pooled
Item sym3 = heap_create_symbol("ok", 2);      // Reuses sym1 pointer

// Long symbols are NOT pooled (arena allocated)
Item sym4 = heap_create_symbol("very_long_symbol_name_exceeding_limit", 42);  // Arena
```

#### 3. Strings (Content Data)
**Function**: `heap_strcpy(const char* str, size_t len)` or `builder.createString()`  
**Pooling**: Never pooled (arena allocated)
**Use Cases**:
- User content text
- String values in documents
- Free-form text data
- Anything that's not a structural identifier

**Benefits**:
- Fast arena allocation (no hash lookup overhead)
- No memory overhead for unique content
- Efficient for one-time strings

**Example**:
```cpp
// Content strings are NOT pooled
Item str1 = builder.createString("Hello, World!");     // Arena allocated
Item str2 = builder.createString("User entered text"); // Arena allocated
Item str3 = builder.createString("Hello, World!");     // Different pointer from str1
```

#### API Decision Guide

| String Type | Function | Pooled? | Use When |
|-------------|----------|---------|----------|
| **Name** | `heap_create_name()` or `builder.createName()` | ✅ Always | Map keys, element tags, attribute names, identifiers |
| **Symbol** | `heap_create_symbol()` | ✅ If ≤32 chars | Symbol literals, short enum-like values |
| **String** | `heap_strcpy()` or `builder.createString()` | ❌ Never | User content, text data, unique values |

**Rule of Thumb**: If it's a structural name that will appear many times, use `createName()`. If it's content data, use `createString()`.

#### NamePool Hierarchy

NamePools support parent-child relationships for schema inheritance:

```cpp
// Schema NamePool (parent)
NamePool* schema_pool = name_pool_create(nullptr);
name_pool_intern_name(schema_pool, "firstName", 9);  // Pooled in schema

// Document NamePool (child inherits from schema)
NamePool* doc_pool = name_pool_create(schema_pool);
const char* name = name_pool_intern_name(doc_pool, "firstName", 9);
// Returns same pointer from parent schema_pool (no duplication)
```

**Benefits**:
- Schema definitions share names with document instances
- No memory duplication for inherited names
- Efficient for validation and transformation pipelines

### Coding Guidelines
- Start comments in lowercase.
- **Add debug logging** for development and troubleshooting.
- **Test with comprehensive nested data structures** and use timeout (default: 5s) to catch hangs early
- **Back up the file** before major refactoring or rewrite. Remove the backup at the end of successful refactoring or rewrite.