# String Pattern Implementation Proposal

## Overview

This document outlines the implementation plan for string pattern types in Lambda Script. String patterns provide compile-time type-safe regular expression matching with a readable syntax.

**Scope of this Phase:**
1. Integrate RE2 library
2. Implement lambda grammar enhancement for string patterns
3. Compile string pattern to regex
4. Support `"str" is pattern` operator

**Deferred to Future Phase:**
- System functions: `find()`, `find_at()`, `replace()`, `split()` with pattern support

---

## 1. RE2 Library Integration

### 1.1 Build Configuration

Add RE2 to `build_lambda_config.json`:

```json
{
    "name": "re2",
    "description": "RE2 regular expression library",
    "include": "/opt/homebrew/Cellar/re2/20250812_1/include",
    "lib": "/opt/homebrew/Cellar/re2/20250812_1/lib/libre2.a",
    "link": "static"
}
```

**Platform Notes:**
- macOS: `/opt/homebrew/Cellar/re2/*/include` and `/opt/homebrew/Cellar/re2/*/lib/libre2.a`
- Linux: Install via `apt install libre2-dev`, paths: `/usr/include` and `/usr/lib/x86_64-linux-gnu/libre2.a`
- Windows: Build from source or use vcpkg

### 1.2 RE2 Wrapper Module

Create `lambda/re2_wrapper.cpp` and `lambda/re2_wrapper.hpp`:

```cpp
// lambda/re2_wrapper.hpp
#pragma once

#include <re2/re2.h>
#include "../lib/mempool.h"
#include "lambda-data.hpp"

// Compiled pattern stored at runtime
typedef struct CompiledPattern {
    TypeId type_id;        // LMD_TYPE_PATTERN (new type)
    uint16_t ref_cnt;      // reference count
    RE2* re2;              // compiled RE2 pattern
    String* source;        // original pattern source (for error messages)
    bool is_full_match;    // whether pattern should match entire string
} CompiledPattern;

// Compile a Lambda pattern expression to RE2 regex
// Returns nullptr on error, sets error message
CompiledPattern* compile_pattern(Pool* pool, const char* pattern_expr, const char** error_msg);

// Match operations
bool pattern_full_match(CompiledPattern* pattern, String* str);
bool pattern_partial_match(CompiledPattern* pattern, String* str);

// Pattern destruction
void pattern_destroy(CompiledPattern* pattern);
```

**Key Design Decisions:**
- Use `RE2::FullMatch` for `is` operator (entire string must match)
- Store compiled pattern in type_list for reuse
- Reference-counted for memory management

---

## 2. Grammar Enhancement

### 2.1 New Grammar Rules

Add to `lambda/tree-sitter-lambda/grammar.js`:

```javascript
// String pattern operators helper
function pattern_operators(pattern_expr) {
  return [
    ['|', 'pattern_union'],      // alternation
    ['&', 'pattern_intersect'],  // intersection
  ].map(([operator, precedence]) =>
    prec.left(precedence, seq(
      field('left', pattern_expr),
      field('operator', operator),
      field('right', pattern_expr),
    )),
  );
}

// Add to rules:

// Character classes for patterns
pattern_char_class: $ => choice(
  '\\d',  // digit [0-9]
  '\\w',  // word [a-zA-Z0-9_]
  '\\s',  // whitespace
  '\\a',  // alpha [a-zA-Z]
  '.',    // any character
),

// Occurrence count for patterns
pattern_count: $ => choice(
  seq('{', $.integer, '}'),           // exactly n
  seq('{', $.integer, ',', '}'),      // n or more  
  seq('{', $.integer, ',', $.integer, '}'),  // n to m
),

// Primary pattern expression
primary_pattern: $ => choice(
  $.string,                  // literal string "abc"
  $.pattern_char_class,      // \d, \w, \s, \a, .
  seq('(', $._pattern_expr, ')'),  // grouping
),

// Pattern with occurrence modifiers
pattern_occurrence: $ => prec.right(seq(
  field('operand', $._pattern_expr),
  field('operator', choice('?', '+', '*', $.pattern_count)),
)),

// Pattern negation
pattern_negation: $ => prec.right(seq(
  '!',
  field('operand', $._pattern_expr),
)),

// Pattern range (character range)
pattern_range: $ => prec.left('pattern_range', seq(
  field('left', $.string),
  'to',
  field('right', $.string),
)),

// Binary pattern expressions
binary_pattern: $ => choice(
  ...pattern_operators($._pattern_expr),
),

// Pattern expression (all forms)
_pattern_expr: $ => choice(
  $.primary_pattern,
  $.pattern_occurrence,
  $.pattern_negation,
  $.pattern_range,
  $.binary_pattern,
),

// String pattern definition
string_pattern: $ => seq(
  'string',
  field('name', $.identifier),
  '=',
  field('pattern', $._pattern_expr),
),

// Symbol pattern definition  
symbol_pattern: $ => seq(
  'symbol',
  field('name', $.identifier),
  '=',
  field('pattern', $._pattern_expr),
),
```

### 2.2 Grammar Integration Points

1. Add `string_pattern` and `symbol_pattern` to `_expr_stam`:
```javascript
_expr_stam: $ => choice(
  $.let_stam,
  $.pub_stam,
  $.fn_expr_stam,
  $.type_stam,
  $.string_pattern,   // NEW
  $.symbol_pattern,   // NEW
),
```

2. Add precedences:
```javascript
precedences: $ => [[
  // ... existing precedences ...
  'pattern_range',
  'pattern_intersect',
  'pattern_union',
  // ...
]],
```

---

## 3. AST Node Types

### 3.1 New AST Node Types

Add to `lambda/ast.hpp`:

```cpp
// In AstNodeType enum:
AST_NODE_STRING_PATTERN,   // string name = pattern
AST_NODE_SYMBOL_PATTERN,   // symbol name = pattern
AST_NODE_PATTERN_RANGE,    // "a" to "z"
AST_NODE_PATTERN_CHAR_CLASS, // \d, \w, \s, \a, .

// New symbol definitions
#define SYM_STRING_PATTERN sym_string_pattern
#define SYM_SYMBOL_PATTERN sym_symbol_pattern
#define SYM_PATTERN_RANGE sym_pattern_range
#define SYM_PATTERN_CHAR_CLASS sym_pattern_char_class
#define SYM_PATTERN_COUNT sym_pattern_count
#define SYM_PATTERN_OCCURRENCE sym_pattern_occurrence
#define SYM_PATTERN_NEGATION sym_pattern_negation
#define SYM_BINARY_PATTERN sym_binary_pattern
#define SYM_PRIMARY_PATTERN sym_primary_pattern

// Character class enum
typedef enum PatternCharClass {
    PATTERN_DIGIT,      // \d
    PATTERN_WORD,       // \w
    PATTERN_SPACE,      // \s
    PATTERN_ALPHA,      // \a
    PATTERN_ANY,        // .
} PatternCharClass;

// Pattern definition node (string name = pattern)
typedef struct AstPatternDefNode : AstNamedNode {
    bool is_symbol;     // true for symbol pattern, false for string pattern
} AstPatternDefNode;

// Pattern range node ("a" to "z")
typedef struct AstPatternRangeNode : AstNode {
    AstNode* start;     // start of range (string literal)
    AstNode* end;       // end of range (string literal)
} AstPatternRangeNode;

// Pattern character class node
typedef struct AstPatternCharClassNode : AstNode {
    PatternCharClass char_class;
} AstPatternCharClassNode;
```

### 3.2 New Type Definition

Add to `lambda/lambda-data.hpp`:

```cpp
// In Operator enum, add:
OPERATOR_PATTERN_UNION,     // | for patterns
OPERATOR_PATTERN_INTERSECT, // & for patterns
OPERATOR_PATTERN_NEGATE,    // ! for patterns
OPERATOR_REPEAT,            // {n}, {n,}, {n,m}

// New type for compiled patterns
typedef struct TypePattern : Type {
    int pattern_index;      // index in pattern_list
    bool is_symbol;         // symbol vs string pattern
} TypePattern;
```

Add to `lambda/lambda.h`:
```c
// New TypeId
LMD_TYPE_PATTERN = 25,  // compiled regex pattern
```

---

## 4. AST Building

### 4.1 Pattern Building Functions

Add to `lambda/build_ast.cpp`:

```cpp
// Forward declarations
AstNode* build_pattern_expr(Transpiler* tp, TSNode node);
AstNode* build_pattern_range(Transpiler* tp, TSNode node);
AstNode* build_pattern_char_class(Transpiler* tp, TSNode node);

// Build pattern range node ("a" to "z")
AstNode* build_pattern_range(Transpiler* tp, TSNode node) {
    log_debug("build pattern range");
    AstPatternRangeNode* ast_node = (AstPatternRangeNode*)
        alloc_ast_node(tp, AST_NODE_PATTERN_RANGE, node, sizeof(AstPatternRangeNode));
    
    TSNode left_node = ts_node_child_by_field_id(node, FIELD_LEFT);
    TSNode right_node = ts_node_child_by_field_id(node, FIELD_RIGHT);
    
    ast_node->start = build_lit_node(tp, left_node);
    ast_node->end = build_lit_node(tp, right_node);
    
    // Type is string pattern
    ast_node->type = alloc_type(tp->pool, LMD_TYPE_PATTERN, sizeof(TypePattern));
    return (AstNode*)ast_node;
}

// Build pattern character class (\d, \w, \s, \a, .)
AstNode* build_pattern_char_class(Transpiler* tp, TSNode node) {
    log_debug("build pattern char class");
    AstPatternCharClassNode* ast_node = (AstPatternCharClassNode*)
        alloc_ast_node(tp, AST_NODE_PATTERN_CHAR_CLASS, node, sizeof(AstPatternCharClassNode));
    
    const char* text = ts_node_text(node, tp->source);
    uint32_t len = ts_node_end_byte(node) - ts_node_start_byte(node);
    
    if (len == 2 && text[0] == '\\') {
        switch (text[1]) {
        case 'd': ast_node->char_class = PATTERN_DIGIT; break;
        case 'w': ast_node->char_class = PATTERN_WORD; break;
        case 's': ast_node->char_class = PATTERN_SPACE; break;
        case 'a': ast_node->char_class = PATTERN_ALPHA; break;
        default: ast_node->char_class = PATTERN_ANY; break;
        }
    } else if (len == 1 && text[0] == '.') {
        ast_node->char_class = PATTERN_ANY;
    }
    
    ast_node->type = alloc_type(tp->pool, LMD_TYPE_PATTERN, sizeof(TypePattern));
    return (AstNode*)ast_node;
}

// Build string/symbol pattern definition
AstNode* build_string_pattern(Transpiler* tp, TSNode node, bool is_symbol) {
    log_debug("build %s pattern", is_symbol ? "symbol" : "string");
    
    AstPatternDefNode* ast_node = (AstPatternDefNode*)
        alloc_ast_node(tp, is_symbol ? AST_NODE_SYMBOL_PATTERN : AST_NODE_STRING_PATTERN,
                       node, sizeof(AstPatternDefNode));
    ast_node->is_symbol = is_symbol;
    
    // Get pattern name
    TSNode name_node = ts_node_child_by_field_id(node, FIELD_NAME);
    ast_node->name = push_name(tp, (AstNamedNode*)ast_node, name_node);
    
    // Get pattern expression
    TSNode pattern_node = ts_node_child_by_field_id(node, FIELD_PATTERN);
    ast_node->as = build_pattern_expr(tp, pattern_node);
    
    // Type is pattern type
    TypePattern* pattern_type = (TypePattern*)alloc_type(tp->pool, LMD_TYPE_PATTERN, sizeof(TypePattern));
    pattern_type->is_symbol = is_symbol;
    ast_node->type = (Type*)pattern_type;
    
    return (AstNode*)ast_node;
}

// Main pattern expression builder
AstNode* build_pattern_expr(Transpiler* tp, TSNode node) {
    TSSymbol symbol = ts_node_symbol(node);
    
    if (symbol == SYM_STRING) {
        return build_lit_node(tp, node);
    }
    else if (symbol == SYM_PATTERN_CHAR_CLASS) {
        return build_pattern_char_class(tp, node);
    }
    else if (symbol == SYM_PATTERN_RANGE) {
        return build_pattern_range(tp, node);
    }
    else if (symbol == SYM_PATTERN_OCCURRENCE) {
        return build_pattern_occurrence(tp, node);
    }
    else if (symbol == SYM_PATTERN_NEGATION) {
        return build_pattern_negation(tp, node);
    }
    else if (symbol == SYM_BINARY_PATTERN) {
        return build_binary_pattern(tp, node);
    }
    else if (symbol == SYM_PRIMARY_PATTERN) {
        // Primary pattern has one child
        TSNode child = ts_node_child(node, 0);
        return build_pattern_expr(tp, child);
    }
    // Handle parenthesized expressions
    uint32_t child_count = ts_node_child_count(node);
    if (child_count == 3) {
        // ( expr )
        return build_pattern_expr(tp, ts_node_child(node, 1));
    }
    
    log_error("Unknown pattern node symbol: %d", symbol);
    return nullptr;
}
```

### 4.2 Integration with build_expr

Add cases to `build_expr()` in `build_ast.cpp`:

```cpp
case SYM_STRING_PATTERN:
    return build_string_pattern(tp, expr_node, false);
case SYM_SYMBOL_PATTERN:
    return build_string_pattern(tp, expr_node, true);
```

---

## 5. Pattern Compilation to Regex

### 5.1 Pattern-to-Regex Compiler

Create compilation logic in `lambda/re2_wrapper.cpp`:

```cpp
// Convert Lambda pattern AST to RE2 regex string
void compile_pattern_node(StrBuf* regex, AstNode* node) {
    switch (node->node_type) {
    case AST_NODE_PRIMARY: {
        // String literal - escape regex metacharacters
        AstPrimaryNode* pri = (AstPrimaryNode*)node;
        TypeString* str_type = (TypeString*)pri->type;
        escape_regex_literal(regex, str_type->string);
        break;
    }
    case AST_NODE_PATTERN_CHAR_CLASS: {
        AstPatternCharClassNode* cc = (AstPatternCharClassNode*)node;
        switch (cc->char_class) {
        case PATTERN_DIGIT: strbuf_append_str(regex, "[0-9]"); break;
        case PATTERN_WORD:  strbuf_append_str(regex, "[a-zA-Z0-9_]"); break;
        case PATTERN_SPACE: strbuf_append_str(regex, "\\s"); break;
        case PATTERN_ALPHA: strbuf_append_str(regex, "[a-zA-Z]"); break;
        case PATTERN_ANY:   strbuf_append_str(regex, "."); break;
        }
        break;
    }
    case AST_NODE_PATTERN_RANGE: {
        AstPatternRangeNode* range = (AstPatternRangeNode*)node;
        // Convert "a" to "z" to [a-z]
        TypeString* start = (TypeString*)range->start->type;
        TypeString* end = (TypeString*)range->end->type;
        strbuf_append_char(regex, '[');
        strbuf_append_char(regex, start->string->chars[0]);
        strbuf_append_char(regex, '-');
        strbuf_append_char(regex, end->string->chars[0]);
        strbuf_append_char(regex, ']');
        break;
    }
    case AST_NODE_BINARY: {
        AstBinaryNode* bin = (AstBinaryNode*)node;
        if (bin->op == OPERATOR_PATTERN_UNION) {
            // a | b -> (?:a|b)
            strbuf_append_str(regex, "(?:");
            compile_pattern_node(regex, bin->left);
            strbuf_append_char(regex, '|');
            compile_pattern_node(regex, bin->right);
            strbuf_append_char(regex, ')');
        } else if (bin->op == OPERATOR_PATTERN_INTERSECT) {
            // a & b -> intersection (requires RE2::Set or lookahead)
            // For now, use positive lookahead: (?=a)b
            strbuf_append_str(regex, "(?=");
            compile_pattern_node(regex, bin->left);
            strbuf_append_char(regex, ')');
            compile_pattern_node(regex, bin->right);
        }
        break;
    }
    case AST_NODE_UNARY: {
        AstUnaryNode* unary = (AstUnaryNode*)node;
        if (unary->op == OPERATOR_OPTIONAL) {
            // a? -> (?:a)?
            strbuf_append_str(regex, "(?:");
            compile_pattern_node(regex, unary->operand);
            strbuf_append_str(regex, ")?");
        } else if (unary->op == OPERATOR_ONE_MORE) {
            // a+ -> (?:a)+
            strbuf_append_str(regex, "(?:");
            compile_pattern_node(regex, unary->operand);
            strbuf_append_str(regex, ")+");
        } else if (unary->op == OPERATOR_ZERO_MORE) {
            // a* -> (?:a)*
            strbuf_append_str(regex, "(?:");
            compile_pattern_node(regex, unary->operand);
            strbuf_append_str(regex, ")*");
        } else if (unary->op == OPERATOR_PATTERN_NEGATE) {
            // !a -> negative lookahead (?!a)
            strbuf_append_str(regex, "(?!");
            compile_pattern_node(regex, unary->operand);
            strbuf_append_char(regex, ')');
        }
        break;
    }
    // Handle count {n}, {n,}, {n,m}
    // ...
    }
}

CompiledPattern* compile_pattern_ast(Pool* pool, AstNode* pattern_ast, const char** error_msg) {
    StrBuf* regex = strbuf_new_cap(256);
    strbuf_append_str(regex, "^");  // anchor start for full match
    compile_pattern_node(regex, pattern_ast);
    strbuf_append_str(regex, "$");  // anchor end
    
    RE2::Options options;
    options.set_log_errors(false);
    
    CompiledPattern* cp = (CompiledPattern*)pool_calloc(pool, sizeof(CompiledPattern));
    cp->type_id = LMD_TYPE_PATTERN;
    cp->re2 = new RE2(regex->str, options);
    
    if (!cp->re2->ok()) {
        *error_msg = cp->re2->error().c_str();
        delete cp->re2;
        strbuf_free(regex);
        return nullptr;
    }
    
    strbuf_free(regex);
    return cp;
}
```

### 5.2 Compile-Time Pattern Registration

During transpilation, compile patterns and register in type_list:

```cpp
// In transpile.cpp, handle pattern definitions
void transpile_pattern_def(Transpiler* tp, AstPatternDefNode* pattern_node) {
    // Compile pattern to regex at compile time
    const char* error_msg = nullptr;
    CompiledPattern* compiled = compile_pattern_ast(tp->pool, pattern_node->as, &error_msg);
    
    if (!compiled) {
        log_error("Pattern compilation failed: %s", error_msg);
        record_error(tp, pattern_node, "Invalid pattern: %s", error_msg);
        return;
    }
    
    // Register in type_list for runtime access
    int pattern_index = arraylist_add(tp->script->type_list, compiled);
    ((TypePattern*)pattern_node->type)->pattern_index = pattern_index;
    
    // No code generation needed - pattern is used via type_list
    log_debug("Registered pattern '%.*s' at index %d",
        (int)pattern_node->name->len, pattern_node->name->chars, pattern_index);
}
```

---

## 6. The `is` Operator Enhancement

### 6.1 Type Checking Enhancement

In `build_ast.cpp`, when building binary expressions with `OPERATOR_IS`:

```cpp
// In build_binary_expr, add pattern type handling:
if (ast_node->op == OPERATOR_IS) {
    // Check if right operand is a pattern type
    if (ast_node->right->type->type_id == LMD_TYPE_PATTERN) {
        // Left must be string (or symbol for symbol patterns)
        TypePattern* pattern = (TypePattern*)ast_node->right->type;
        TypeId expected = pattern->is_symbol ? LMD_TYPE_SYMBOL : LMD_TYPE_STRING;
        if (ast_node->left->type->type_id != expected &&
            ast_node->left->type->type_id != LMD_TYPE_ANY) {
            log_error("Pattern match requires %s, got type %d",
                pattern->is_symbol ? "symbol" : "string",
                ast_node->left->type->type_id);
        }
    }
    type_id = LMD_TYPE_BOOL;
}
```

### 6.2 Runtime Implementation

Enhance `fn_is` in `lambda/lambda-eval.cpp`:

```cpp
Bool fn_is(Item a, Item b) {
    log_debug("fn_is");
    TypeId b_type_id = get_type_id(b);
    
    // Handle pattern matching
    if (b_type_id == LMD_TYPE_PATTERN) {
        CompiledPattern* pattern = (CompiledPattern*)b.container;
        TypeId a_type_id = get_type_id(a);
        
        // Check type compatibility
        if (pattern->is_symbol && a_type_id != LMD_TYPE_SYMBOL) {
            log_error("Symbol pattern requires symbol value");
            return BOOL_ERROR;
        }
        if (!pattern->is_symbol && a_type_id != LMD_TYPE_STRING) {
            log_error("String pattern requires string value");
            return BOOL_ERROR;
        }
        
        String* str = a.get_string();
        re2::StringPiece input(str->chars, str->len);
        bool matched = RE2::FullMatch(input, *pattern->re2);
        return matched ? BOOL_TRUE : BOOL_FALSE;
    }
    
    // Existing type checking logic...
    if (b_type_id != LMD_TYPE_TYPE) {
        log_error("2nd argument must be a type or pattern, got type: %d", b_type_id);
        return BOOL_ERROR;
    }
    // ... rest of existing fn_is implementation
}
```

### 6.3 Transpilation

In `transpile.cpp`, handle pattern reference:

```cpp
void transpile_pattern_ref(Transpiler* tp, AstIdentNode* ident_node) {
    // Pattern lookup - emit code to get pattern from type_list
    NameEntry* entry = ident_node->entry;
    if (entry && entry->node->node_type == AST_NODE_STRING_PATTERN) {
        AstPatternDefNode* pattern_def = (AstPatternDefNode*)entry->node;
        TypePattern* type = (TypePattern*)pattern_def->type;
        strbuf_append_format(tp->code_buf, "const_pattern(%d)", type->pattern_index);
    }
}
```

Add runtime function:
```cpp
// In lambda-eval.cpp
Item const_pattern(int pattern_index) {
    ArrayList* type_list = context->type_list;
    CompiledPattern* pattern = (CompiledPattern*)type_list->data[pattern_index];
    Item result;
    result.container = (Container*)pattern;
    return result;
}
```

Register in `mir.c`:
```c
{"const_pattern", (fn_ptr) const_pattern},
```

---

## 7. Implementation Steps

### Phase 1: RE2 Integration (1-2 days)
1. Add RE2 to build config
2. Create `re2_wrapper.hpp/cpp`
3. Test RE2 compilation and matching
4. Add `LMD_TYPE_PATTERN` to type system

### Phase 2: Grammar Extension (1-2 days)
1. Add pattern grammar rules to `grammar.js`
2. Run `make generate-grammar`
3. Verify generated symbols in `ts-enum.h`
4. Test parsing with tree-sitter

### Phase 3: AST Building (1-2 days)
1. Add AST node types to `ast.hpp`
2. Implement build functions in `build_ast.cpp`
3. Add symbol definitions
4. Test AST construction

### Phase 4: Pattern Compilation (2-3 days)
1. Implement pattern-to-regex compiler
2. Handle all pattern operators
3. Register patterns in type_list
4. Test compilation

### Phase 5: `is` Operator (1-2 days)
1. Enhance type checking in build_ast.cpp
2. Update `fn_is` for pattern matching
3. Add transpilation support
4. Test pattern matching

### Phase 6: Testing & Documentation (1-2 days)
1. Add unit tests for pattern compilation
2. Add integration tests for `is` operator
3. Update documentation

---

## 8. Test Cases

```lambda
// Basic pattern definition
string digit = "0" to "9"
string letter = "a" to "z" | "A" to "Z"
string alphanumeric = letter | digit

// Occurrence operators
string digits = digit+
string optional_sign = ("-" | "+")?
string integer = optional_sign digits

// Character classes
string word = \w+
string whitespace = \s*

// Negation
string non_digit = !\d

// Test matching
"123" is digits        // true
"abc" is digits        // false
"Hello" is letter+     // true
"-42" is integer       // true
"3.14" is integer      // false
```

---

## 9. File Changes Summary

| File | Changes |
|------|---------|
| `build_lambda_config.json` | Add RE2 library |
| `lambda/re2_wrapper.hpp` | NEW: RE2 wrapper header |
| `lambda/re2_wrapper.cpp` | NEW: RE2 wrapper implementation |
| `lambda/tree-sitter-lambda/grammar.js` | Pattern grammar rules |
| `lambda/ast.hpp` | New AST node types |
| `lambda/lambda.h` | LMD_TYPE_PATTERN |
| `lambda/lambda-data.hpp` | TypePattern struct, operators |
| `lambda/build_ast.cpp` | Pattern building functions |
| `lambda/transpile.cpp` | Pattern transpilation |
| `lambda/lambda-eval.cpp` | fn_is enhancement, const_pattern |
| `lambda/mir.c` | Register const_pattern |

---

## 10. Risks and Mitigations

| Risk | Mitigation |
|------|------------|
| RE2 doesn't support all regex features | Document limitations; use RE2::Set for intersection |
| Pattern compilation errors at runtime | Compile at parse time, catch errors early |
| Performance overhead | Cache compiled patterns in type_list |
| Complex intersection patterns | Use RE2::Set or document as limitation |

---

## 11. Future Considerations (Out of Scope)

- Capture groups support
- System functions: `find()`, `find_at()`, `replace()`, `split()` with patterns
- Pattern literal syntax (no `string` keyword)
- Named capture groups
- Pattern composition/reuse
