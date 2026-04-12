# JavaScript Transpiler v2: Unified Architecture Proposal

## Executive Summary

This document proposes a comprehensive redesign of the JavaScript transpiler to achieve true integration with the Lambda runtime. The current implementation (v1) suffers from fundamental architectural issues that prevent it from supporting advanced JavaScript features. The v2 design focuses on **unified type systems**, **reusing Lambda's closure/function infrastructure**, and **leveraging the existing MIR JIT pipeline** rather than duplicating it.

## Current Implementation Analysis

### Issues Identified

#### 1. **Duplicate/Parallel Infrastructure (Critical)**

The current JS transpiler creates its own parallel infrastructure instead of reusing Lambda's:

| Component | Lambda | JS Current | Issue |
|-----------|--------|------------|-------|
| AST Nodes | `AstNode`, `AstFuncNode`, etc. | `JsAstNode`, `JsFunctionNode`, etc. | Duplicated, incompatible |
| Scope | `NameScope`, `NameEntry` | `JsScope`, reuses `NameEntry` | Partial reuse, inconsistent |
| Function | `Function`, `CaptureInfo`, `TypeFunc` | `JsFunction` (separate) | No closure support |
| Type System | `Type`, `TypeId` | Incomplete mapping | Missing types, wrong mappings |

**Result**: The JS runtime cannot call Lambda functions, closures don't work, and the MIR JIT pipeline cannot be used.

#### 2. **Incorrect Type Mappings**

```cpp
// WRONG: BigInt should map to Decimal, not Int64
JS_BIGINT → LMD_TYPE_INT64   // BigInt is arbitrary precision!

// WRONG: Both null and undefined map to the same value
JS_NULL → LMD_TYPE_NULL
JS_UNDEFINED → LMD_TYPE_NULL  // Loses distinction!
```

Correct mapping should be:
```cpp
JS_BIGINT → LMD_TYPE_DECIMAL   // Lambda Decimal is arbitrary precision
JS_UNDEFINED → ITEM_UNDEFINED  // Need special undefined value
JS_NULL → ITEM_NULL            // Keep null separate
```

#### 3. **Bypassing MIR JIT Compilation**

The current implementation has a bizarre workaround in `js_transpiler_compile()`:
```cpp
// Extract variable assignments and execute them (HACK!)
char* a_assign = strstr(code_ptr, "Item _js_a = d2it(");
// ... manually parsing generated C code ...
```

This completely bypasses the MIR JIT compiler and only works for the simplest arithmetic expressions.

#### 4. **Function/Closure Infrastructure Missing**

The JS implementation has a barebones `JsFunction` struct:
```cpp
typedef struct JsFunction {
    void* func_ptr;
    int param_count;
    Item* closure_vars;   // TODO: Not implemented
    int closure_count;
} JsFunction;
```

Lambda already has a sophisticated closure system:
```cpp
struct Function {
    uint8_t type_id;
    uint8_t arity;
    uint16_t ref_cnt;
    void* fn_type;        // TypeFunc*
    fn_ptr ptr;
    void* closure_env;    // Proper closure environment
    const char* name;
};
```

#### 5. **`this` Binding Not Implemented**

JavaScript's `this` keyword requires special handling:
- Global context: `this` = global object
- Method call: `this` = object
- Arrow function: lexical `this`
- `call`/`apply`/`bind`: explicit `this`

The current implementation has no `this` support.

#### 6. **Prototype Chain Not Implemented**

JavaScript objects have prototype chains; the current implementation only supports flat maps.

---

## Proposed Architecture: v2 Design

### Core Principles

1. **Reuse Lambda's AST** where possible, extend only when necessary
2. **Single type system** - map JS types to Lambda types correctly
3. **Use Lambda's Function/Closure infrastructure** for JS functions
4. **Generate code that feeds into the existing MIR pipeline**
5. **Implement JS semantics via runtime bridge functions**

### Type System Unification

#### Correct Type Mapping

| JavaScript Type | Lambda Type | Notes |
|-----------------|-------------|-------|
| `undefined` | `ITEM_UNDEFINED` | New: Add `LMD_TYPE_UNDEFINED` |
| `null` | `ITEM_NULL` | Existing |
| `boolean` | `LMD_TYPE_BOOL` | Existing |
| `number` | `LMD_TYPE_FLOAT` | All JS numbers are float64 |
| `bigint` | `LMD_TYPE_DECIMAL` | Arbitrary precision |
| `string` | `LMD_TYPE_STRING` | Existing |
| `symbol` | `LMD_TYPE_SYMBOL` | Existing |
| `function` | `LMD_TYPE_FUNC` | Use Lambda's Function struct |
| `object` | `LMD_TYPE_MAP` | Extended with prototype chain |
| `array` | `LMD_TYPE_ARRAY` | Existing |

#### Add `LMD_TYPE_UNDEFINED`

```cpp
// In lambda.h, add to EnumTypeId:
enum EnumTypeId {
    LMD_TYPE_RAW_POINTER = 0,
    LMD_TYPE_NULL,
    LMD_TYPE_UNDEFINED,  // NEW: JavaScript undefined
    // ...
};

#define ITEM_UNDEFINED  ((uint64_t)LMD_TYPE_UNDEFINED << 56)
```

### AST Strategy: Transform to Lambda AST

Instead of maintaining a separate JS AST, transform JavaScript constructs to Lambda AST nodes during the build phase:

```
JavaScript Source 
    → Tree-sitter Parse Tree 
    → JS-specific preprocessing 
    → Lambda AST 
    → Lambda Transpiler 
    → C code 
    → MIR JIT
```

#### Mapping JavaScript Constructs to Lambda AST

| JavaScript | Lambda AST | Notes |
|------------|------------|-------|
| `function f(a, b) { ... }` | `AST_NODE_FUNC` | Standard function |
| `(a, b) => a + b` | `AST_NODE_FUNC_EXPR` | Arrow as lambda |
| `var x = 5` | `AST_NODE_VAR_STAM` | Procedural var |
| `let x = 5` | `AST_NODE_LET_STAM` | Block-scoped let |
| `if (c) { ... }` | `AST_NODE_IF_STAM` | Conditional |
| `while (c) { ... }` | `AST_NODE_WHILE_STAM` | Loop |
| `for (;;) { ... }` | `AST_NODE_FOR_STAM` | Loop |
| `{ key: value }` | `AST_NODE_MAP` | Object literal |
| `[1, 2, 3]` | `AST_NODE_ARRAY` | Array literal |
| `obj.prop` | `AST_NODE_MEMBER_EXPR` | Member access |
| `fn(args)` | `AST_NODE_CALL_EXPR` | Function call |

### JavaScript-Specific Runtime Layer

Instead of generating different code, use runtime bridge functions that implement JavaScript semantics:

```cpp
// js_runtime.h - JavaScript runtime bridge

// Type coercion (existing, keep)
Item js_to_primitive(Item value, const char* hint);
Item js_to_number(Item value);
Item js_to_string(Item value);
Item js_to_boolean(Item value);

// Operators (existing, enhance)
Item js_add(Item left, Item right);        // String concat or numeric add
Item js_equal(Item left, Item right);      // == with coercion
Item js_strict_equal(Item left, Item right); // ===

// NEW: this binding support
Item js_bind_this(Function* fn, Item this_binding);
Item js_call_with_this(Function* fn, Item this_binding, Item* args, int argc);

// NEW: Prototype chain
typedef struct JsObjectMeta {
    Map* prototype;      // [[Prototype]] internal slot
    Map* own_properties; // Own properties
    Function* constructor;
} JsObjectMeta;

Item js_get_property(Item object, Item key);  // With prototype chain lookup
Item js_set_property(Item object, Item key, Item value);
Item js_has_property(Item object, Item key);  // in operator
Item js_delete_property(Item object, Item key);

// NEW: Object creation
Item js_create_object(Item prototype);
Item js_object_create(Item prototype, Item properties);

// NEW: Array methods using Lambda's existing functions
Item js_array_map(Item array, Function* callback);
Item js_array_filter(Item array, Function* callback);
Item js_array_reduce(Item array, Function* reducer, Item initial);
Item js_array_forEach(Item array, Function* callback);
```

### Function & Closure Integration

#### Reuse Lambda's `Function` Structure

```cpp
// Use Lambda's existing Function struct
struct Function {
    uint8_t type_id;
    uint8_t arity;
    uint16_t ref_cnt;
    void* fn_type;        // TypeFunc*
    fn_ptr ptr;
    void* closure_env;    // For closures
    const char* name;
};

// JS-specific wrapper for 'this' binding
typedef struct JsBoundFunction {
    Function base;        // Inherit from Lambda Function
    Item this_binding;    // Bound 'this' value
    bool is_arrow;        // Arrow functions have lexical 'this'
} JsBoundFunction;
```

#### Closure Capture

Reuse Lambda's `CaptureInfo` and closure environment generation:

```cpp
// In build_js_ast.cpp, when building function:
void analyze_js_captures(JsTranspiler* tp, JsFunctionNode* fn) {
    // Walk function body and identify free variables
    // Create CaptureInfo list exactly like Lambda does
    CaptureInfo* captures = NULL;
    
    // ... analyze body for free variables ...
    
    // Store in the Lambda-compatible AstFuncNode
    AstFuncNode* lambda_fn = (AstFuncNode*)fn->lambda_ast;
    lambda_fn->captures = captures;
}
```

### Code Generation Strategy

#### Option A: Direct AST Conversion (Recommended)

Convert JS AST directly to Lambda AST, then use Lambda's existing transpiler:

```cpp
// js_to_lambda_ast.cpp

AstNode* convert_js_to_lambda(JsAstNode* js_node) {
    switch (js_node->node_type) {
    case JS_AST_NODE_FUNCTION_DECLARATION: {
        JsFunctionNode* js_fn = (JsFunctionNode*)js_node;
        
        // Create Lambda function node
        AstFuncNode* lambda_fn = (AstFuncNode*)pool_alloc(tp->ast_pool, sizeof(AstFuncNode));
        lambda_fn->node_type = AST_NODE_PROC;  // JS functions are procedural
        lambda_fn->name = js_fn->name;
        lambda_fn->param = convert_js_params(js_fn->params);
        lambda_fn->body = convert_js_to_lambda(js_fn->body);
        lambda_fn->captures = NULL;  // Filled by capture analysis
        
        return (AstNode*)lambda_fn;
    }
    
    case JS_AST_NODE_BINARY_EXPRESSION: {
        JsBinaryNode* js_bin = (JsBinaryNode*)js_node;
        
        if (js_bin->op == JS_OP_ADD) {
            // JS + operator needs runtime dispatch
            // Generate: js_add(left, right)
            AstCallNode* call = create_runtime_call("js_add", 2);
            call->arguments = create_arg_list(
                convert_js_to_lambda(js_bin->left),
                convert_js_to_lambda(js_bin->right)
            );
            return (AstNode*)call;
        }
        // ... other operators ...
    }
    
    // ... other node types ...
    }
}
```

#### Option B: Modified JS Code Generation

Keep separate JS code generation but ensure it produces Lambda-compatible C code:

```cpp
void transpile_js_function(JsTranspiler* tp, JsFunctionNode* fn) {
    // Generate function using Lambda's naming convention
    strbuf_append_str(tp->code_buf, "\nItem _");
    strbuf_append_str(tp->code_buf, fn->name->chars);
    strbuf_append_int(tp->code_buf, ts_node_start_byte(fn->base.node));
    strbuf_append_str(tp->code_buf, "(");
    
    // Generate closure env parameter if needed (like Lambda)
    if (fn->has_captures) {
        strbuf_append_str(tp->code_buf, "void* _env, ");
    }
    
    // Parameters
    JsAstNode* param = fn->params;
    while (param) {
        strbuf_append_str(tp->code_buf, "Item ");
        transpile_js_identifier(tp, (JsIdentifierNode*)param);
        param = param->next;
        if (param) strbuf_append_str(tp->code_buf, ", ");
    }
    
    strbuf_append_str(tp->code_buf, ") {\n");
    
    // Unpack closure environment (like Lambda)
    if (fn->has_captures) {
        strbuf_append_str(tp->code_buf, "  Env_f");
        strbuf_append_int(tp->code_buf, ts_node_start_byte(fn->base.node));
        strbuf_append_str(tp->code_buf, "* env = (Env_f");
        strbuf_append_int(tp->code_buf, ts_node_start_byte(fn->base.node));
        strbuf_append_str(tp->code_buf, "*)_env;\n");
    }
    
    // Function body
    transpile_js_statement(tp, fn->body);
    
    strbuf_append_str(tp->code_buf, "\n}\n");
}
```

### `this` Binding Implementation (Phase 3 - Deferred)

*Deferred until Lambda object system is ready*

```cpp
// Thread 'this' through function calls
typedef struct JsCallContext {
    Item this_binding;
    Item* arguments;
    int arg_count;
} JsCallContext;

// For method calls: obj.method(args)
void transpile_js_method_call(JsTranspiler* tp, JsMemberNode* member, JsCallNode* call) {
    // Evaluate object (will be 'this')
    strbuf_append_str(tp->code_buf, "({ Item _this = ");
    transpile_js_expression(tp, member->object);
    strbuf_append_str(tp->code_buf, "; ");
    
    // Get method from object
    strbuf_append_str(tp->code_buf, "Item _fn = js_get_property(_this, ");
    transpile_js_property_key(tp, member->property);
    strbuf_append_str(tp->code_buf, "); ");
    
    // Call with 'this' bound
    strbuf_append_str(tp->code_buf, "js_call_with_this(_fn.function, _this, (Item[]){");
    transpile_js_arguments(tp, call->arguments);
    strbuf_append_str(tp->code_buf, "}, ");
    strbuf_append_int(tp->code_buf, count_arguments(call->arguments));
    strbuf_append_str(tp->code_buf, "); })");
}

// Arrow functions capture lexical 'this'
void transpile_js_arrow_function(JsTranspiler* tp, JsFunctionNode* fn) {
    // Arrow functions inherit 'this' from enclosing scope
    // Add 'this' to capture list
    if (tp->current_this_scope) {
        add_capture(fn, "_this", tp->current_this_scope->this_binding);
    }
    
    // Generate as regular closure but mark as arrow
    transpile_js_function(tp, fn);
}
```

### Prototype Chain Implementation (Phase 3 - Deferred)

*Deferred until Lambda object system is ready*

```cpp
// Extended Map for JS objects
typedef struct JsObject {
    Map base;               // Inherit from Lambda Map
    struct JsObject* __proto__;  // Prototype link
    uint32_t flags;         // Object flags (extensible, sealed, frozen)
} JsObject;

Item js_get_property(Item object, Item key) {
    if (object.type_id() != LMD_TYPE_MAP) {
        // Handle primitives (auto-boxing)
        return js_get_primitive_property(object, key);
    }
    
    JsObject* obj = (JsObject*)object.map;
    Item key_str = js_to_string(key);
    
    // Own property lookup
    Item value = map_get((Map*)obj, key_str);
    if (value.item != ITEM_UNDEFINED) {
        return value;
    }
    
    // Prototype chain lookup
    JsObject* proto = obj->__proto__;
    while (proto) {
        value = map_get((Map*)proto, key_str);
        if (value.item != ITEM_UNDEFINED) {
            return value;
        }
        proto = proto->__proto__;
    }
    
    return (Item){.item = ITEM_UNDEFINED};
}
```

### Integration with MIR JIT

The generated C code must be compatible with Lambda's MIR pipeline:

```cpp
Item js_transpiler_compile_v2(JsTranspiler* tp, Runtime* runtime) {
    // 1. Parse JS source
    TSNode root = ts_tree_root_node(tp->tree);
    
    // 2. Build JS AST
    JsAstNode* js_ast = build_js_ast(tp, root);
    
    // 3. Analyze captures (like Lambda)
    analyze_all_captures(tp, js_ast);
    
    // 4. Convert to Lambda AST OR generate Lambda-compatible C code
    // Option A:
    AstNode* lambda_ast = convert_js_to_lambda_ast(tp, js_ast);
    // Then use Lambda's transpile functions
    
    // Option B:
    // Generate JS-specific C code that uses Lambda runtime
    transpile_js_ast_root(tp, js_ast);
    
    // 5. Compile via MIR (reuse Lambda's pipeline)
    char* c_code = tp->code_buf->str;
    
    MIR_context_t ctx = jit_init(runtime->optimize_level);
    jit_compile_to_mir(ctx, c_code, strlen(c_code), "js_module");
    
    // 6. Get main function
    void* main_fn = jit_gen_func(ctx, "_js_main");
    
    // 7. Execute
    typedef Item (*js_main_t)(void);
    Item result = ((js_main_t)main_fn)();
    
    jit_cleanup(ctx);
    return result;
}
```

---

## Implementation Phases

### Phase 1: Foundation (Week 1-2) - ✅ COMPLETED

1. ✅ **Add `LMD_TYPE_UNDEFINED`** to type system
2. ✅ **Fix type mappings** (especially BigInt → Decimal)
3. ✅ **Integrate with MIR JIT pipeline** (remove the string parsing hack)
4. ✅ **Basic literal expressions** working through MIR

**Success Criteria**: ✅ `simple_test.js` passes with MIR JIT

### Phase 2: Core Language Features (Week 3-6) - ✅ COMPLETED

**Status**: ✅ COMPLETED

Focus: **Type System, Expressions, Control Flow, Functions**

#### 2.1 Complete Type System
1. ✅ **All primitive types** working (undefined, null, boolean, number, string)
2. ✅ **Type coercion** functions (js_to_number, js_to_string, js_to_boolean)
3. ✅ **Array literals** with indexing

#### 2.2 Expressions
1. ✅ **Arithmetic operators** (+, -, *, /, %, **)
2. ✅ **Comparison operators** (==, ===, !=, !==, <, <=, >, >=)
3. ✅ **Logical operators** (&&, ||, !)
4. ✅ **Bitwise operators** (&, |, ^, ~, <<, >>, >>>)
5. ✅ **Assignment operators** (=, +=, -=, *=, /=, %=)
6. ✅ **Ternary operator** (? :)
7. ✅ **typeof operator**

#### 2.3 Control Flow
1. ✅ **if/else statements**
2. ✅ **while loops**
3. ✅ **for loops** (C-style: for(init; cond; update))
4. ✅ **break and continue**
5. ✅ **Block scoping** for let/const

#### 2.4 Functions
1. ✅ **Function declarations** (`function f(a, b) { ... }`)
2. ✅ **Function expressions** (`var f = function(a, b) { ... }`)
3. ✅ **Arrow functions** (`(a, b) => a + b`) - without `this` binding for now
4. ✅ **Reuse Lambda's `Function` struct**
5. ✅ **Basic closure support** using Lambda's capture analysis
6. ✅ **Proper scoping** (var = function scope, let/const = block scope)
7. ✅ **Return statements**

**Success Criteria**: ✅ `basic_expressions.js`, `functions.js`, `control_flow.js` pass

### Phase 3: Objects, Arrays, & Modern Syntax (Partial - COMPLETED)

**Status**: ✅ COMPLETED (except `this` keyword and method calls - deferred)

#### 3.1 Completed Features
1. ✅ **Object literals** - `{name: "test", value: 42}`
2. ✅ **Property access** - `obj.name`, `obj["key"]`
3. ✅ **Array literals** - `[1, 2, 3, 4, 5]`
4. ✅ **Array indexing** - `arr[0]`, `arr[i]`
5. ✅ **Arrow functions** - `(a, b) => a + b` and `x => x * 2`
6. ✅ **Template literals** - `` `Hello ${name}!` ``
7. ✅ **let/const declarations** - block-scoped (treated like var for now)

#### 3.2 Deferred Features (Need Lambda Object System)
- **`this` keyword** binding for methods
- **Method calls** with correct `this` context
- **Arrow functions** with lexical `this`
- **Prototype chain** implementation
- **Constructor functions** and `new` operator
- **Property descriptors** (getters/setters)

#### 3.3 Implementation Notes

**Arrow Function Single Parameter**: Tree-sitter uses `"parameter"` (singular) field for arrow functions without parentheses like `x => x * 2`. Updated `build_js_function()` to check both `"parameters"` and `"parameter"` fields.

**Template Literal Bug Fix**: The `s2it()` macro evaluates its argument twice due to the ternary:
```c
#define s2it(str_ptr) ((str_ptr)? ((((uint64_t)LMD_TYPE_STRING)<<56) | (uint64_t)(str_ptr)): null)
```
When `stringbuf_to_string(buf)` was passed directly, it was called twice - first call succeeded and cleared the buffer, second call returned NULL. Fixed by storing result in temp variable first:
```c
String* _template_result = stringbuf_to_string(template_buf);
s2it(_template_result);
```

**Success Criteria**: ✅ `phase3_test.js` passes - all features verified working together

### Phase 4: Advanced ES6 Features (Future)

1. **ES6 classes** (syntactic sugar over prototypes)
2. **Spread operator** for arrays and function calls
3. **Destructuring** (basic array/object)
4. **Array methods** (map, filter, reduce, forEach)
5. **Built-in objects** (Math, JSON, Date basics)
6. **Rest parameters** (`function f(...args)`)
7. **Default parameters** (`function f(x = 10)`)

**Success Criteria**: `es6_features.js`, `array_methods.js` pass

### Phase 5: Error Handling (Future)

*Deferred - requires runtime exception infrastructure*

1. **try/catch/finally** using setjmp/longjmp
2. **throw** statement
3. **Error objects** (Error, TypeError, ReferenceError)
4. **Stack traces** for errors

**Success Criteria**: `error_handling.js` passes

---

## File Structure Changes

```
lambda/
├── js/
│   ├── js_ast.hpp              # JS-specific AST extensions (minimal)
│   ├── js_transpiler.hpp       # JS transpiler interface
│   ├── build_js_ast.cpp        # JS parsing and AST building
│   ├── js_to_lambda.cpp        # NEW: Convert JS AST to Lambda AST
│   ├── transpile_js.cpp        # JS code generation (uses Lambda patterns)
│   ├── js_runtime.cpp          # JS runtime functions
│   ├── js_runtime.h            # NEW: Runtime function declarations
│   ├── js_object.cpp           # NEW: JS object/prototype implementation
│   └── js_builtins.cpp         # NEW: Built-in objects (Array, Object, etc.)
├── lambda.h                    # Add LMD_TYPE_UNDEFINED
├── lambda-data.hpp             # Type extensions
└── transpiler.hpp              # Integration point
```

---

## Testing Strategy

### Unit Tests

Each phase should have corresponding tests:

```cpp
// test/test_js_v2_gtest.cpp

TEST(JavaScriptV2, TypeMappingUndefined) {
    Item undef = execute_js("undefined");
    ASSERT_EQ(undef.type_id(), LMD_TYPE_UNDEFINED);
}

TEST(JavaScriptV2, BigIntToDecimal) {
    Item bigint = execute_js("123456789012345678901234567890n");
    ASSERT_EQ(bigint.type_id(), LMD_TYPE_DECIMAL);
}

TEST(JavaScriptV2, ClosureCapture) {
    const char* js = R"(
        function outer(x) {
            return function inner(y) {
                return x + y;
            };
        }
        outer(10)(5);
    )";
    Item result = execute_js(js);
    ASSERT_EQ(it2d(result), 15.0);
}

// Phase 3 (deferred): this binding
// TEST(JavaScriptV2, ThisBinding) { ... }
```

### Integration Tests

Test JavaScript/Lambda interoperability:

```cpp
TEST(JavaScriptV2, CallLambdaFromJS) {
    // Load Lambda function
    run_script(runtime, "let double = fn(x) x * 2");
    
    // Call from JavaScript
    Item result = execute_js("double(21)");
    ASSERT_EQ(it2d(result), 42.0);
}
```

---

## Appendix: Code Examples

### Example 1: Variable Declaration Transpilation

**JavaScript:**
```javascript
let x = 5;
const y = "hello";
var z = true;
```

**Generated C (v2):**
```c
// Block-scoped let
Item _x = d2it(5.0);

// Block-scoped const (same as let for now)
Item _y = s2it("hello");

// Function-scoped var
Item _z = b2it(true);
```

### Example 2: Function with Closure

**JavaScript:**
```javascript
function counter(start) {
    let count = start;
    return function() {
        count = count + 1;
        return count;
    };
}
```

**Generated C (v2):**
```c
// Closure environment struct
typedef struct Env_f123 {
    Item count;
} Env_f123;

// Inner function (closure)
Item _anon456(void* _env) {
    Env_f123* env = (Env_f123*)_env;
    env->count = js_add(env->count, d2it(1.0));
    return env->count;
}

// Outer function
Item _counter123(Item start) {
    // Allocate closure environment
    Env_f123* env = (Env_f123*)heap_calloc(sizeof(Env_f123), LMD_TYPE_RAW_POINTER);
    env->count = start;
    
    // Return closure
    return (Item){.function = to_closure((fn_ptr)_anon456, 0, env)};
}
```

### Example 3: Object with Method

**JavaScript:**
```javascript
var person = {
    name: "Alice",
    greet: function() {
        return "Hello, " + this.name;
    }
};
person.greet();
```

**Generated C (v2):**
```c
// Method function
Item _greet789(Item _this) {
    Item name = js_get_property(_this, s2it("name"));
    return js_add(s2it("Hello, "), name);
}

// Object creation
Item _person = js_new_object();
js_set_property(_person, s2it("name"), s2it("Alice"));
js_set_property(_person, s2it("greet"), 
    (Item){.function = to_fn_n((fn_ptr)_greet789, 1)});

// Method call with 'this' binding
Item result = js_call_with_this(
    js_get_property(_person, s2it("greet")).function,
    _person,
    NULL, 0
);
```

---

## Conclusion

The v2 JavaScript transpiler design addresses the fundamental issues in v1 by:

1. **Unifying the type system** with correct mappings
2. **Reusing Lambda's infrastructure** (Function, CaptureInfo, MIR JIT)
3. **Implementing JavaScript semantics** via runtime bridge functions
4. **Supporting core language features first** (types, expressions, control flow, functions)

### Phase Summary

| Phase | Focus | Tests Enabled | Status |
|-------|-------|---------------|--------|
| 1 | Foundation + MIR integration | `simple_test.js` | ✅ Complete |
| 2 | Types, expressions, control flow, functions | `basic_expressions.js`, `functions.js`, `control_flow.js` | ✅ Complete |
| 3 | Objects, arrays, arrow functions, template literals, let/const | `phase3_test.js` | ✅ Complete (partial) |
| 3b | `this` keyword, method calls | `advanced_features.js` | ⏳ Deferred |
| 4 | ES6 features (spread, destructuring, classes) | `es6_features.js`, `array_methods.js` | ⏳ Deferred |
| 5 | Error handling (try/catch) | `error_handling.js` | ⏳ Deferred |

### Follow-up Actions

1. **Immediate**: Consider adding more comprehensive tests for template literals with nested expressions
2. **Phase 3b**: Implement `this` keyword when Lambda object model supports method binding
3. **Phase 4**: Spread operator and destructuring are high-value features for user adoption
4. **Technical Debt**: The `s2it()` macro double-evaluation issue could affect other code - audit usages
5. **Documentation**: Add examples of working Phase 3 features to test suite

The phased implementation approach prioritizes core language features that can be built on Lambda's existing infrastructure, deferring object system features until Lambda's own object model is enhanced.
