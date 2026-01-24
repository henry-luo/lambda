# Lambda Member Function Support Proposal

## Executive Summary

This proposal outlines enhancements to Lambda to support **member function syntax**, enabling:
1. **System Function as Member**: Calling sys funcs using method-style syntax (e.g., `item.len()` instead of `len(item)`)
2. **Type Checking**: Validating that the object type matches the first parameter of the sys func
3. **Type-Defined Methods**: Allowing types to define their own member functions (similar to JS/TS prototypes)
4. **Extension Methods**: User-defined functions attachable to existing types

---

## 1. Motivation

### 1.1 Current State

Lambda currently uses **prefix function call syntax** for all operations:

```lambda
// Current syntax
len(items)                  // get length
slice(str, 0, 5)           // substring
contains(text, "hello")    // check containment
reverse(list)              // reverse a list
string(42)                 // type conversion
```

### 1.2 Proposed Enhancement

Support **method-style syntax** as syntactic sugar, where the object becomes the first argument:

```lambda
// Proposed syntax (equivalent to the above)
items.len()                 // same as len(items)
str.slice(0, 5)            // same as slice(str, 0, 5)
text.contains("hello")     // same as contains(text, "hello")
list.reverse()             // same as reverse(list)
42.string()                // same as string(42)
```

### 1.3 Benefits

| Benefit | Description |
|---------|-------------|
| **Readability** | Method chaining flows left-to-right: `data.filter(f).map(g).sort()` |
| **Discoverability** | IDE can suggest methods applicable to a type |
| **Familiarity** | Aligns with JS/Python/Rust conventions |
| **Flexibility** | Both styles remain valid; user chooses per context |

---

## 2. Part 1: System Functions as Methods

### 2.1 Mechanism: Method Call Desugaring

A method call `obj.method(args...)` is desugared at parse/AST time into `method(obj, args...)`.

```
Source:        obj.method(a, b)
Desugared:     method(obj, a, b)
```

This transformation happens in `build_ast.cpp` when processing `call_expr` nodes where the function is a `member_expr`.

### 2.2 Grammar Extension

The current grammar already supports `member_expr`:

```javascript
// grammar.js (existing)
member_expr: $ => seq(
  field('object', $.primary_expr), ".",
  field('field', choice($.identifier, $.symbol, $.index))
),

call_expr: $ => seq(
  field('function', choice($.primary_expr, $.import)),
  $._arguments,
),
```

**Proposed Change**: Allow `member_expr` as the function in `call_expr`:

```javascript
// grammar.js (proposed modification)
call_expr: $ => seq(
  field('function', choice($.primary_expr, $.member_expr, $.import)),
  $._arguments,
),
```

Or handle in AST building by detecting when `primary_expr` contains a `member_expr` followed by arguments.

### 2.3 AST Building Phase

In `build_ast.cpp`, when building a `call_expr`:

```cpp
// build_ast.cpp - build_call_expr()
AstNode* build_call_expr(Transpiler* tp, TSNode call_node) {
    TSNode fn_node = ts_node_child_by_field_name(call_node, "function", 8);
    
    // Check if function is a member_expr (obj.method)
    if (ts_node_symbol(fn_node) == SYM_MEMBER_EXPR) {
        // Extract object and method name
        TSNode obj_node = ts_node_child_by_field_name(fn_node, "object", 6);
        TSNode method_node = ts_node_child_by_field_name(fn_node, "field", 5);
        
        // Get method name as string
        StrView method_name = get_node_text(tp, method_node);
        
        // Count arguments (excluding object)
        int arg_count = count_arguments(call_node) + 1;  // +1 for object
        
        // Look up system function: method_name with arg_count
        SysFuncInfo* sys_fn = get_sys_func_info(&method_name, arg_count);
        
        if (sys_fn) {
            // Transform: obj.method(args) -> method(obj, args)
            return build_sysfunc_call_with_object(tp, sys_fn, obj_node, call_node);
        } else {
            // Not a sys func - could be user-defined method (Part 3)
            return build_method_call(tp, fn_node, call_node);
        }
    }
    
    // Regular function call (unchanged)
    // ...
}
```

### 2.4 Eligible System Functions

Not all sys funcs make sense as methods. Functions eligible for method-style calls should:

1. **Have at least one parameter** (the "self" object)
2. **First parameter accepts the object type** (type compatibility check)
3. **Semantically operate on the first argument** (not unrelated to it)

#### 2.4.1 Recommended Sys Funcs for Method Syntax

| Category | Function | Method Form | Description |
|----------|----------|-------------|-------------|
| **Type** | `len(x)` | `x.len()` | Get length |
| **Type** | `type(x)` | `x.type()` | Get type |
| **Type** | `string(x)` | `x.string()` | Convert to string |
| **Type** | `int(x)` | `x.int()` | Convert to int |
| **Type** | `float(x)` | `x.float()` | Convert to float |
| **String** | `slice(s, i, j)` | `s.slice(i, j)` | Substring |
| **String** | `contains(s, sub)` | `s.contains(sub)` | Check containment |
| **String** | `normalize(s, form)` | `s.normalize(form)` | Unicode normalize |
| **Collection** | `reverse(c)` | `c.reverse()` | Reverse collection |
| **Collection** | `sort(c)` | `c.sort()` | Sort collection |
| **Collection** | `unique(c)` | `c.unique()` | Remove duplicates |
| **Collection** | `concat(a, b)` | `a.concat(b)` | Concatenate |
| **Collection** | `take(c, n)` | `c.take(n)` | Take first n |
| **Collection** | `drop(c, n)` | `c.drop(n)` | Drop first n |
| **Math** | `abs(x)` | `x.abs()` | Absolute value |
| **Math** | `round(x)` | `x.round()` | Round |
| **Math** | `floor(x)` | `x.floor()` | Floor |
| **Math** | `ceil(x)` | `x.ceil()` | Ceiling |
| **Math** | `sqrt(x)` | `x.sqrt()` | Square root |
| **Stats** | `sum(v)` | `v.sum()` | Sum elements |
| **Stats** | `avg(v)` | `v.avg()` | Average |
| **Stats** | `min(v)` | `v.min()` | Minimum |
| **Stats** | `max(v)` | `v.max()` | Maximum |
| **DateTime** | `date(dt)` | `dt.date()` | Extract date |
| **DateTime** | `time(dt)` | `dt.time()` | Extract time |

#### 2.4.2 Functions NOT Suitable for Method Syntax

| Function | Reason |
|----------|--------|
| `min(a, b)` | Two equal operands, no clear "self" |
| `max(a, b)` | Two equal operands, no clear "self" |
| `fill(n, value)` | First param is count, not the subject |
| `range(start, end)` | Constructs new range, no object input |
| `datetime()` | Zero-argument constructor |
| `today()` | Zero-argument function |
| `print(x)` | Side-effect procedure, not transformation |

### 2.5 Type Annotation for Method Eligibility

Add a flag to `SysFuncInfo` to indicate method eligibility:

```cpp
// build_ast.cpp
struct SysFuncInfo {
    SysFunc id;
    const char* name;
    int arg_count;
    Type* return_type;
    bool is_proc;
    bool is_overloaded;
    bool is_method_eligible;   // NEW: can be called as method
    TypeId first_param_type;   // NEW: expected type of first param (TYPE_ANY for any)
};

SysFuncInfo sys_funcs[] = {
    {SYSFUNC_LEN, "len", 1, &TYPE_INT64, false, false, true, TYPE_ANY},
    {SYSFUNC_TYPE, "type", 1, &TYPE_TYPE, false, false, true, TYPE_ANY},
    {SYSFUNC_SLICE, "slice", 3, &TYPE_ANY, false, false, true, TYPE_ANY},
    {SYSFUNC_CONTAINS, "contains", 2, &TYPE_BOOL, false, false, true, TYPE_STRING},
    // ...
};
```

---

## 3. Part 2: Type Checking for Method Calls

### 3.1 First Parameter Type Validation

When resolving `obj.method(args)`, verify that `obj`'s type is compatible with the first parameter of `method`.

```cpp
// Type checking in build_ast.cpp or transpile.cpp
bool validate_method_call(Type* obj_type, SysFuncInfo* sys_fn) {
    // TYPE_ANY matches everything
    if (sys_fn->first_param_type == TYPE_ANY) {
        return true;
    }
    
    // Exact match
    if (obj_type->type_id == sys_fn->first_param_type) {
        return true;
    }
    
    // Numeric coercion: int/float/decimal -> number
    if (sys_fn->first_param_type == TYPE_NUMBER) {
        return is_numeric_type(obj_type);
    }
    
    // Collection compatibility: array/list -> collection
    if (sys_fn->first_param_type == TYPE_COLLECTION) {
        return is_collection_type(obj_type);
    }
    
    return false;
}
```

### 3.2 Error Messages

Provide clear error messages for type mismatches:

```
Error: Method 'contains' is not available for type 'int'
  Hint: 'contains' expects a string or collection as receiver

Error: Method 'sum' is not available for type 'string'
  Hint: 'sum' expects a numeric array or list
```

### 3.3 Compile-Time vs Runtime Checking

- **Compile-time**: When object type is statically known, validate at AST build
- **Runtime**: When object type is `any` or dynamic, defer check to runtime

```cpp
// At transpile time
if (obj_type->type_id == TYPE_ANY) {
    // Generate runtime type check
    emit_runtime_method_check(tp, obj_node, sys_fn);
} else {
    // Validate now
    if (!validate_method_call(obj_type, sys_fn)) {
        type_error(tp, "Method '%s' not available for type '%s'", 
                   sys_fn->name, type_name(obj_type));
    }
}
```

---

## 4. Part 3: User-Defined Type Methods

### 4.1 Syntax for Type Method Definition

Allow methods to be defined within type declarations:

```lambda
// Define a Point type with methods
type Point {
    x: int
    y: int
    
    // Method definition
    fn distance(other: Point): float {
        sqrt((self.x - other.x)^2 + (self.y - other.y)^2)
    }
    
    fn scale(factor: float): Point {
        Point { x: self.x * factor, y: self.y * factor }
    }
    
    // Computed property (no params)
    fn magnitude(): float {
        sqrt(self.x^2 + self.y^2)
    }
}

// Usage
let p1 = Point { x: 3, y: 4 }
let p2 = Point { x: 0, y: 0 }

p1.distance(p2)    // 5.0
p1.magnitude()     // 5.0
p1.scale(2.0)      // Point { x: 6, y: 8 }
```

### 4.2 The `self` Keyword

Within type methods, `self` refers to the instance:

| Context | `self` Meaning |
|---------|----------------|
| Method body | The object instance the method is called on |
| Field access | `self.field` accesses instance fields |
| Method call | `self.method()` calls another method |

### 4.3 Grammar Extension for Type Methods

```javascript
// grammar.js additions

type_method: $ => seq(
    'fn',
    field('name', $.identifier),
    field('params', $._parameters),
    optional(seq(':', field('return_type', $._type_expr))),
    field('body', $.block)
),

object_type: $ => seq(
    'type',
    field('name', $.identifier),
    optional(seq('extends', field('extends', $._type_expr))),
    '{',
    repeat(choice(
        $.type_field,      // existing: field declarations
        $.type_method,     // NEW: method declarations
    )),
    '}'
),
```

### 4.4 AST Representation

```cpp
// ast.hpp additions

typedef struct AstMethodNode : AstNode {
    String* name;           // method name
    AstNamedNode* params;   // parameters (excluding implicit self)
    AstNode* body;          // method body
    Type* return_type;      // return type
    bool is_static;         // class method vs instance method
} AstMethodNode;

// Extend TypeObject to include methods
typedef struct TypeObject : Type {
    String* name;
    ShapeEntry* fields;     // existing: field definitions
    AstMethodNode* methods; // NEW: linked list of methods
    Type* extends;          // parent type (if any)
} TypeObject;
```

### 4.5 Method Resolution Order

When `obj.method()` is called:

1. **Check type-defined methods** on `obj`'s type
2. **Check parent type methods** (if type extends another)
3. **Check sys funcs** that match the method name and accept obj's type
4. **Error** if not found

```cpp
AstNode* resolve_method(Type* obj_type, StrView* method_name, int arg_count) {
    // 1. Check type's own methods
    if (obj_type->type_id == TYPE_OBJECT) {
        TypeObject* obj_t = (TypeObject*)obj_type;
        AstMethodNode* method = find_method(obj_t->methods, method_name);
        if (method) return (AstNode*)method;
        
        // 2. Check parent type
        if (obj_t->extends) {
            AstNode* parent_method = resolve_method(obj_t->extends, method_name, arg_count);
            if (parent_method) return parent_method;
        }
    }
    
    // 3. Check sys funcs
    SysFuncInfo* sys_fn = get_sys_func_info(method_name, arg_count + 1);
    if (sys_fn && sys_fn->is_method_eligible) {
        if (validate_method_call(obj_type, sys_fn)) {
            return (AstNode*)sys_fn;
        }
    }
    
    return NULL;  // 4. Not found
}
```

---

## 5. Part 4: Extension Methods

### 5.1 Concept

Allow users to add methods to existing types (including built-in types) without modifying the type definition:

```lambda
// Extend the string type with custom methods
extend string {
    fn words(): array {
        // split by whitespace (placeholder impl)
        split(self, " ")
    }
    
    fn capitalize(): string {
        if (len(self) == 0) ""
        else upper(self[0]) ++ slice(self, 1, len(self))
    }
}

// Usage
"hello world".words()       // ["hello", "world"]
"hello".capitalize()        // "Hello"
```

### 5.2 Grammar for Extension

```javascript
// grammar.js
extend_stam: $ => seq(
    'extend',
    field('type', choice($._base_type, $.identifier)),
    '{',
    repeat($.type_method),
    '}'
),
```

### 5.3 Scoping and Visibility

Extension methods are:
- **Scoped to the module** where they are defined
- **Not globally visible** (to avoid conflicts)
- **Imported explicitly** with module import

```lambda
// math_ext.ls
extend int {
    fn factorial(): int {
        if (self <= 1) 1
        else self * (self - 1).factorial()
    }
}

// main.ls
import "math_ext.ls"
5.factorial()    // 120
```

### 5.4 Precedence Rules

When multiple methods match:

| Priority | Source | Example |
|----------|--------|---------|
| 1 (highest) | Type-defined method | `Point.distance()` |
| 2 | Extension method (current module) | `extend Point { fn foo() }` |
| 3 | Extension method (imported module) | Imported extensions |
| 4 (lowest) | System function | `len()`, `string()`, etc. |

---

## 6. Implementation Roadmap

### Phase 1: Sys Func as Methods (MVP) ✅ COMPLETED

**Scope**: Transform `obj.method(args)` → `method(obj, args)` for eligible sys funcs.

**Changes Implemented**:
1. ✅ Grammar: `member_expr` already supported as call target
2. ✅ `build_ast.cpp`: Added `get_sys_func_for_method()` lookup function; modified `build_call_expr()` to detect member-call pattern and desugar by prepending object as first argument
3. ✅ `SysFuncInfo`: Added `is_method_eligible` flag to all 60+ sys func entries
4. ✅ `transpile.cpp`: Fixed sys func name generation to use `fn_info->name` for method calls
5. ✅ Tests: `test/lambda/method_call.ls` with expected output

**Completed**: January 2026

### Phase 2: Type Checking ✅ COMPLETED

**Scope**: Validate object type against first parameter type.

**Changes Implemented**:
1. ✅ `SysFuncInfo`: Added `first_param_type` field (TypeId) to all sys func entries
2. ✅ Type validation logic in `get_sys_func_for_method()` - checks `first_param_type` compatibility
3. ✅ All baseline tests pass (101/101)

**Completed**: January 2026

### Phase 3: Type-Defined Methods

**Scope**: Allow `type T { fn method() { ... } }` syntax.

**Changes Required**:
1. Grammar: Add `type_method` rule
2. AST: Add `AstMethodNode`, extend `TypeObject`
3. Method resolution in `build_ast.cpp`
4. Transpilation: Generate method code with `self` binding
5. Tests: Custom types with methods

**Effort**: ~5-7 days

### Phase 4: Extension Methods

**Scope**: Allow `extend T { fn method() { ... } }` syntax.

**Changes Required**:
1. Grammar: Add `extend_stam` rule
2. Extension registry in transpiler state
3. Method resolution update (check extensions)
4. Module-scoped visibility
5. Tests: Extending built-in types

**Effort**: ~3-5 days

---

## 7. Additional Suggestions

### 7.1 Method Chaining Syntax

Support fluent API patterns:

```lambda
// Chained method calls
data
    .filter(x => x > 0)
    .map(x => x * 2)
    .take(10)
    .sum()
```

This works naturally once method syntax is supported.

### 7.2 Optional Chaining (`?.`)

Support null-safe method calls:

```lambda
// Optional chaining - returns null if receiver is null
user?.name?.len()

// Equivalent to:
if (user != null) {
    if (user.name != null) {
        user.name.len()
    } else null
} else null
```

**Grammar**:
```javascript
optional_member_expr: $ => seq(
    field('object', $.primary_expr), "?.",
    field('field', choice($.identifier, $.symbol))
),
```

### 7.3 Static Methods

Allow type-level (static) methods:

```lambda
type Point {
    x: int
    y: int
    
    // Instance method
    fn scale(factor: float): Point { ... }
    
    // Static method (no self)
    static fn origin(): Point {
        Point { x: 0, y: 0 }
    }
}

Point.origin()    // Point { x: 0, y: 0 }
```

### 7.4 Property Syntax (Getter Methods)

Allow zero-parameter methods to be called without `()`:

```lambda
type Circle {
    radius: float
    
    // Property (getter)
    prop area: float {
        3.14159 * self.radius^2
    }
}

let c = Circle { radius: 5 }
c.area           // 78.54 (no parentheses needed)
c.area()         // Also works, same result
```

### 7.5 Computed Properties with `get`/`set`

For mutable contexts (procedural mode):

```lambda
type Rectangle {
    var width: float
    var height: float
    
    prop area: float {
        get { self.width * self.height }
        set(value) { 
            // Maintain aspect ratio
            let ratio = self.width / self.height
            self.height = sqrt(value / ratio)
            self.width = self.height * ratio
        }
    }
}
```

### 7.6 Protocol/Interface Support

Define method contracts that types must implement:

```lambda
protocol Comparable {
    fn compare(other: Self): int
}

type Point implements Comparable {
    x: int
    y: int
    
    fn compare(other: Point): int {
        let d1 = self.x^2 + self.y^2
        let d2 = other.x^2 + other.y^2
        if (d1 < d2) -1
        else if (d1 > d2) 1
        else 0
    }
}
```

---

## 8. Compatibility Considerations

### 8.1 Backward Compatibility

- **Prefix syntax remains valid**: `len(x)` still works
- **No breaking changes**: Existing code unchanged
- **Gradual adoption**: Users can use either style

### 8.2 Ambiguity Resolution

If `obj.method` could be either a field access or method:

| Priority | Resolution |
|----------|------------|
| 1 | Field access (if `method` is a known field of obj's type) |
| 2 | Method call (if followed by `()`) |

```lambda
type Foo {
    len: int       // field named 'len'
}

let f = Foo { len: 5 }
f.len            // Field access: 5
f.len()          // Error: field 'len' is not callable
len(f)           // Sys func: 2 (number of fields)
```

### 8.3 Reserved Method Names

Some names might conflict between fields and methods:
- `len`, `type`, `string` - commonly both sys funcs and potential field names
- Resolution: Fields take precedence for `obj.name`, methods for `obj.name()`

---

## 9. Examples

### 9.1 String Operations

```lambda
// Traditional style
let s = "Hello, World!"
len(s)                          // 13
slice(s, 0, 5)                  // "Hello"
contains(s, "World")            // true

// Method style
s.len()                         // 13
s.slice(0, 5)                   // "Hello"
s.contains("World")             // true

// Chained
s.slice(7, 12).len()            // 5
```

### 9.2 Array Processing

```lambda
let nums = [3, 1, 4, 1, 5, 9, 2, 6]

// Traditional
sum(sort(unique(nums)))         // 30

// Method style (more readable)
nums.unique().sort().sum()      // 30

// Mixed (both work)
nums.unique().sort() |> sum     // 30 (with pipe operator)
```

### 9.3 Custom Type with Methods

```lambda
type Vector2D {
    x: float
    y: float
    
    fn add(other: Vector2D): Vector2D {
        Vector2D { x: self.x + other.x, y: self.y + other.y }
    }
    
    fn dot(other: Vector2D): float {
        self.x * other.x + self.y * other.y
    }
    
    fn length(): float {
        sqrt(self.x^2 + self.y^2)
    }
    
    fn normalize(): Vector2D {
        let len = self.length()
        Vector2D { x: self.x / len, y: self.y / len }
    }
}

let v1 = Vector2D { x: 3, y: 4 }
let v2 = Vector2D { x: 1, y: 0 }

v1.length()           // 5.0
v1.dot(v2)            // 3.0
v1.normalize()        // Vector2D { x: 0.6, y: 0.8 }
v1.add(v2).length()   // 5.0
```

---

## 10. Summary

| Feature | Description | Priority |
|---------|-------------|----------|
| **Sys Func as Method** | `obj.method(args)` → `method(obj, args)` | P0 (MVP) |
| **Type Checking** | Validate receiver type for method calls | P0 (MVP) |
| **Type Methods** | Define methods within type declarations | P1 |
| **Extension Methods** | Add methods to existing types | P2 |
| **Optional Chaining** | `obj?.method()` null-safe calls | P2 |
| **Static Methods** | Type-level methods without instance | P2 |
| **Properties** | Zero-param methods as properties | P3 |
| **Protocols** | Interface/trait contracts | P3 |

The member function syntax enhances Lambda's expressiveness while maintaining its functional nature. The implementation can be phased, starting with sys func desugaring (simple AST transformation) and building toward full type-defined methods.
