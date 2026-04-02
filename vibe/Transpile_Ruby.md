# Proposal: Ruby Support Under Lambda

> **Goal**: Add Ruby language support to the Lambda runtime using the same architecture
> as LambdaJS and LambdaPy — Tree-sitter parsing, typed AST construction, direct MIR
> JIT compilation, and unified `Item` runtime data types — with full cross-language
> module interop from day one.

---

## 1. Executive Summary

### Problem

Lambda currently supports JS/TS, Python, and Bash as guest languages. Ruby shares
significant semantic overlap with Python (OOP, blocks/closures, dynamic typing) and
with Lambda itself (functional idioms, expression-oriented syntax). There is no
lightweight, JIT-compiled Ruby execution environment that shares a runtime data model
with other languages — every existing Ruby implementation (CRuby, JRuby, TruffleRuby)
is a standalone VM.

### Solution

Add **LambdaRuby** — a Ruby transpiler that follows the proven LambdaJS/LambdaPy
architecture:

1. **Parse** Ruby source with `tree-sitter-ruby` into a CST.
2. **Build** a typed Ruby AST (`RbAstNode`) from the CST.
3. **Transpile** the AST directly to MIR IR (no intermediate C code).
4. **Execute** via MIR JIT — all values are Lambda `Item` (64-bit tagged pointers).

All Ruby values are Lambda values. A Ruby hash is a Lambda `Map`. A Ruby array is a
Lambda `Array`. A Ruby string is a Lambda `String`. This enables **zero-copy
cross-language interop** — a Ruby function can call a Python function or import a
Lambda module with no marshalling.

### CLI Interface

```bash
./lambda.exe rb script.rb             # Run a Ruby script
./lambda.exe rb -e "puts 2 + 3"      # Evaluate inline Ruby
```

### Key Benefits

| Benefit | Detail |
|---------|--------|
| Unified runtime | All Ruby values are `Item` — shared GC, name pool, arena |
| Cross-language imports | `require_relative 'helper.py'` or `require_relative 'util.ls'` |
| JIT performance | MIR native codegen, same optimization playbook as JS/Python |
| Minimal binary impact | tree-sitter-ruby grammar adds ~200–400KB; transpiler ~8–15K LOC |
| Reuse | 150+ system functions, `MarkBuilder`/`MarkReader`, RE2 regex, I/O — all shared |

---

## 2. Architecture

### 2.1 Pipeline Overview

```
Ruby source (.rb)
    │
    ▼
┌─────────────────────────────┐
│  tree-sitter-ruby parser    │   CST (concrete syntax tree)
│  (libtree-sitter-ruby.a)    │
└─────────────┬───────────────┘
              │
              ▼
┌─────────────────────────────┐
│  build_rb_ast.cpp           │   RbAstNode* typed AST
│  (CST → Ruby AST)          │
└─────────────┬───────────────┘
              │
              ▼
┌─────────────────────────────┐
│  transpile_rb_mir.cpp       │   MIR IR emission
│  (AST → MIR instructions)  │   All values boxed as Item (MIR_T_I64)
└─────────────┬───────────────┘
              │
              ▼
┌─────────────────────────────┐
│  MIR JIT (mir.c)            │   Native machine code
│  import_resolver for        │
│  rb_* runtime functions     │
└─────────────┬───────────────┘
              │
              ▼
         Execution
   (Item in, Item out)
```

This is identical to the LambdaPy pipeline:

| Component | Python | Ruby |
|-----------|--------|------|
| Grammar | `tree-sitter-python/` | `tree-sitter-ruby/` |
| AST header | `py_ast.hpp` | `rb_ast.hpp` |
| AST builder | `build_py_ast.cpp` | `build_rb_ast.cpp` |
| Transpiler struct | `py_transpiler.hpp` | `rb_transpiler.hpp` |
| MIR transpiler | `transpile_py_mir.cpp` | `transpile_rb_mir.cpp` |
| Runtime functions | `py_runtime.cpp` | `rb_runtime.cpp` |
| Class/OOP support | `py_class.cpp` | `rb_class.cpp` |
| Built-in methods | `py_builtins.cpp` | `rb_builtins.cpp` |
| Std library | `py_stdlib.cpp` | `rb_stdlib.cpp` |
| Scope management | `py_scope.cpp` | `rb_scope.cpp` |

### 2.2 Unified Runtime Data Model

All Ruby values map to existing Lambda `Item` types — **no new type IDs required**:

| Ruby Type | Lambda Item Type | Representation |
|-----------|-----------------|----------------|
| `Integer` | `LMD_TYPE_INT` / `LMD_TYPE_INT64` | 56-bit inline or 64-bit heap |
| `Float` | `LMD_TYPE_FLOAT` | 64-bit IEEE 754 double |
| `true` / `false` | `LMD_TYPE_BOOL` | Inline tagged |
| `nil` | `LMD_TYPE_NULL` | `ItemNull` |
| `String` | `LMD_TYPE_STRING` | GC-managed `String*` via name pool |
| `Symbol` | `LMD_TYPE_SYMBOL` | Interned via name pool (`s2it`) |
| `Array` | `LMD_TYPE_ARRAY` | Lambda `Array` container |
| `Hash` | `LMD_TYPE_MAP` | Lambda `Map` with `ShapeEntry` chain |
| `Range` | `LMD_TYPE_RANGE` | Lambda `Range` container |
| `Proc` / `Lambda` / `Method` | `LMD_TYPE_FUNCTION` | Lambda `Function*` |
| `Regexp` | `LMD_TYPE_STRING` + RE2 | RE2 wrapper (same as JS regex) |
| Class instance | `LMD_TYPE_MAP` | Shaped map (same as Python class instances) |

**Key design choice**: Ruby symbols map directly to Lambda symbols via the name pool.
`:foo` in Ruby becomes the same interned symbol ID as `'foo` in Lambda. This enables
efficient symbol-keyed hash lookups and seamless cross-language symbol passing.

### 2.3 Transpiler Context Struct

Following the Python transpiler pattern:

```c
// rb_transpiler.hpp

typedef enum RbVarKind {
    RB_VAR_LOCAL,       // local variable
    RB_VAR_IVAR,        // instance variable (@x)
    RB_VAR_CVAR,        // class variable (@@x)
    RB_VAR_GVAR,        // global variable ($x)
    RB_VAR_CONST,       // constant (UPPER or CamelCase)
    RB_VAR_BLOCK,       // block parameter
    RB_VAR_FREE,        // captured from enclosing scope (closure)
    RB_VAR_CELL,        // local but captured by inner block/proc
} RbVarKind;

typedef enum RbScopeType {
    RB_SCOPE_TOP,       // top-level (main)
    RB_SCOPE_METHOD,    // def method
    RB_SCOPE_CLASS,     // class body
    RB_SCOPE_MODULE,    // module body
    RB_SCOPE_BLOCK,     // block/proc/lambda
} RbScopeType;

typedef struct RbScope {
    RbScopeType scope_type;
    NameEntry* first;
    NameEntry* last;
    struct RbScope* parent;
    RbMethodDefNode* method;    // associated method (if method scope)
} RbScope;

typedef struct RbTranspiler {
    // core components
    Pool* ast_pool;
    NamePool* name_pool;
    StrBuf* code_buf;
    const char* source;
    size_t source_length;

    // scoping
    RbScope* current_scope;
    RbScope* top_scope;

    // compilation state
    int method_counter;
    int temp_var_counter;
    int label_counter;
    int block_depth;            // track block nesting for yield

    // error handling
    bool has_errors;
    StrBuf* error_buf;

    // tree-sitter
    TSParser* parser;
    TSTree* tree;

    // runtime
    Runtime* runtime;
} RbTranspiler;
```

### 2.4 AST Node Types

```c
// rb_ast.hpp

typedef enum RbAstNodeType {
    RB_AST_NODE_NULL,

    // program
    RB_AST_NODE_PROGRAM,

    // statements
    RB_AST_NODE_EXPRESSION_STATEMENT,
    RB_AST_NODE_ASSIGNMENT,
    RB_AST_NODE_OP_ASSIGNMENT,          // +=, -=, *=, etc.
    RB_AST_NODE_RETURN,
    RB_AST_NODE_IF,
    RB_AST_NODE_UNLESS,
    RB_AST_NODE_WHILE,
    RB_AST_NODE_UNTIL,
    RB_AST_NODE_FOR,
    RB_AST_NODE_CASE,
    RB_AST_NODE_WHEN,
    RB_AST_NODE_BREAK,
    RB_AST_NODE_NEXT,                   // Ruby's "continue"
    RB_AST_NODE_REDO,
    RB_AST_NODE_METHOD_DEF,
    RB_AST_NODE_CLASS_DEF,
    RB_AST_NODE_MODULE_DEF,
    RB_AST_NODE_BEGIN_RESCUE,           // begin/rescue/ensure
    RB_AST_NODE_RESCUE,
    RB_AST_NODE_ENSURE,
    RB_AST_NODE_RAISE,
    RB_AST_NODE_YIELD,
    RB_AST_NODE_BLOCK,

    // expressions
    RB_AST_NODE_IDENTIFIER,
    RB_AST_NODE_SELF,
    RB_AST_NODE_LITERAL,               // int, float, string, symbol, nil, true, false
    RB_AST_NODE_STRING_INTERPOLATION,   // "hello #{name}"
    RB_AST_NODE_BINARY_OP,
    RB_AST_NODE_UNARY_OP,
    RB_AST_NODE_BOOLEAN_OP,            // and, or, not, &&, ||, !
    RB_AST_NODE_COMPARISON,
    RB_AST_NODE_CALL,                  // method call
    RB_AST_NODE_ATTRIBUTE,             // obj.attr
    RB_AST_NODE_SUBSCRIPT,             // obj[key]
    RB_AST_NODE_ARRAY,
    RB_AST_NODE_HASH,
    RB_AST_NODE_RANGE,                 // 1..10, 1...10
    RB_AST_NODE_PROC_LAMBDA,           // proc { } / lambda { } / -> { }
    RB_AST_NODE_BLOCK_PASS,            // method(&block)
    RB_AST_NODE_SPLAT,                 // *args
    RB_AST_NODE_DOUBLE_SPLAT,          // **kwargs
    RB_AST_NODE_PAIR,                  // key => value or key: value in hash
    RB_AST_NODE_PARAMETER,
    RB_AST_NODE_DEFAULT_PARAMETER,
    RB_AST_NODE_IVAR,                  // @instance_var
    RB_AST_NODE_CVAR,                  // @@class_var
    RB_AST_NODE_GVAR,                  // $global_var
    RB_AST_NODE_CONST,                 // Constant / ClassName
    RB_AST_NODE_TERNARY,               // cond ? a : b
    RB_AST_NODE_HEREDOC,

    RB_AST_NODE_COUNT
} RbAstNodeType;
```

---

## 3. Ruby-to-Item Semantics Mapping

### 3.1 Operators

All operators transpile to runtime calls with full type dispatch (v1 approach, matching
LambdaPy's initial strategy — optimization via type inference comes in later phases):

| Ruby | Runtime function | Notes |
|------|-----------------|-------|
| `a + b` | `rb_add(Item, Item)` | Numeric add, string concat, array concat |
| `a - b` | `rb_sub(Item, Item)` | |
| `a * b` | `rb_mul(Item, Item)` | String/array repetition for `str * n` |
| `a / b` | `rb_div(Item, Item)` | Integer division for int/int (unlike Python) |
| `a % b` | `rb_mod(Item, Item)` | Sign-of-divisor semantics (same as Python) |
| `a ** b` | `rb_pow(Item, Item)` | |
| `a == b` | `rb_eq(Item, Item)` | Value equality; calls `.==` method |
| `a <=> b` | `rb_cmp(Item, Item)` | Spaceship operator → -1, 0, 1 |
| `a === b` | `rb_case_eq(Item, Item)` | Case equality (used in `when`) |
| `a.equal?(b)` | `rb_identical(Item, Item)` | Identity (same object) |
| `a << b` | `rb_shovel(Item, Item)` | Append (array/string) or left shift (int) |
| `a & b` | `rb_bitand(Item, Item)` | Array intersection or bitwise AND |
| `a \| b` | `rb_bitor(Item, Item)` | Array union or bitwise OR |

### 3.2 Ruby-Specific Idioms

| Ruby idiom | Transpilation strategy |
|-----------|----------------------|
| `yield` | Compiled as call to the block parameter (an `Item` of type `Function`) |
| `block_given?` | Check whether the implicit block param is non-null |
| `&block` | Passes `Proc` as the implicit block parameter |
| `proc { }` / `lambda { }` | Create `Function*` item with closure capture |
| `->(){}` (stabby lambda) | Same as `lambda`, with arity enforcement |
| `x..y` / `x...y` | `Range` container (inclusive/exclusive end) |
| `attr_reader :x` | Generates getter method → shaped property read |
| `attr_writer :x=` | Generates setter method → shaped property write |
| `attr_accessor :x` | Both getter and setter |
| `@ivar` | Instance variable → shaped slot on `self` map |
| `@@cvar` | Class variable → property on the class map |
| `$gvar` | Global variable → slot in a global map |
| `CONST` | Constant → module-level scoped binding |
| `include ModName` | Mixin → copy module methods into class method table |
| `require_relative` | Cross-language module import (see §5) |

### 3.3 Control Flow

| Ruby construct | MIR translation |
|---------------|----------------|
| `if` / `elsif` / `else` / `end` | `MIR_BF` / `MIR_JMP` branches |
| `unless` | Inverted `MIR_BT` |
| `while` / `until` | Loop with `MIR_BF`/`MIR_BT` + back-edge |
| `for x in collection` | Iterator protocol: `rb_get_iterator` + `rb_iterator_next` |
| `case` / `when` | Chain of `rb_case_eq` comparisons + `MIR_BT` |
| `begin` / `rescue` / `ensure` | `setjmp`/`longjmp` exception frames (same as Python try/except) |
| `break` / `next` / `redo` | `MIR_JMP` to loop labels |
| Statement modifiers (`x if cond`) | Same as `if cond then x end` |
| `&&` / `\|\|` short-circuit | `MIR_BF`/`MIR_BT` with value propagation |

---

## 4. Reuse of Existing Infrastructure

### 4.1 System Functions (Direct Reuse)

Many Lambda system functions map directly to Ruby built-in methods. These are already
registered in `sys_func_registry.c` and compiled via MIR — no new code needed:

| Ruby method | Lambda sys function | Notes |
|-------------|-------------------|-------|
| `x.length` / `x.size` | `len(x)` | Strings, arrays, hashes |
| `x.to_s` | `str(x)` / `to_string(x)` | String conversion |
| `x.to_i` | `to_int(x)` | Integer conversion |
| `x.to_f` | `to_float(x)` | Float conversion |
| `x.class` | `type(x)` | Type name string |
| `x.nil?` | `is_null(x)` | Null check |
| `x.is_a?(T)` | `is_type(x, t)` | Type check |
| `x.abs` | `abs(x)` | Absolute value |
| `x.round(n)` | `round(x, n)` | Rounding |
| `[a, b].min` | `min(a, b)` | Minimum |
| `[a, b].max` | `max(a, b)` | Maximum |
| `x.floor` | `floor(x)` | Floor |
| `x.ceil` | `ceil(x)` | Ceiling |
| `Math.sqrt(x)` | `sqrt(x)` | Square root |
| `Math.sin(x)` | `sin(x)` | Trigonometric |
| `Math.cos(x)` | `cos(x)` | Trigonometric |
| `Math.log(x)` | `ln(x)` | Natural log |
| `x.split(sep)` | `split(x, sep)` | String split |
| `x.strip` | `trim(x)` | Whitespace trim |
| `x.include?(s)` | `contains(x, s)` | Substring/element check |
| `x.start_with?(s)` | `starts_with(x, s)` | Prefix check |
| `x.end_with?(s)` | `ends_with(x, s)` | Suffix check |
| `x.replace(a, b)` | `replace(x, a, b)` | String replacement |
| `x.reverse` | `reverse(x)` | String/array reverse |
| `x.sort` | `sort(x)` | Array sort |
| `x.map { }` | `map(x, fn)` | Collection map |
| `x.select { }` | `filter(x, fn)` | Collection filter |
| `x.reduce(init) { }` | `reduce(x, fn, init)` | Collection reduce |
| `x.each { }` | `each(x, fn)` | Iteration |
| `x.flatten` | `flatten(x)` | Array flatten |
| `x.compact` | `compact(x)` | Remove nils |
| `x.uniq` | `unique(x)` | Unique elements |
| `x.zip(y)` | `zip(x, y)` | Zip arrays |
| `x.join(sep)` | `join(x, sep)` | Array join |
| `File.read(path)` | `read_file(path)` | File I/O |
| `File.write(path, s)` | `write_file(path, s)` | File I/O |
| `puts x` | `print(x)` | Output |
| `x.match(re)` | RE2 `regex_match(x, re)` | Regex |

### 4.2 Shared Infrastructure (Zero Additional Code)

| Component | What it provides |
|-----------|-----------------|
| `MarkBuilder` / `MarkReader` | Construction and traversal of Lambda data structures |
| Name pool (`name_pool.hpp`) | String/symbol interning — Ruby `:symbol` uses same pool |
| Arena allocator | Short-lived value allocation |
| GC heap (`lambda-mem.cpp`) | Long-lived container management |
| Shape pool (`shape_pool.hpp`) | Shaped property access for class instances |
| RE2 regex (`re2_wrapper.hpp`) | `Regexp` implementation |
| Module registry (`module_registry.cpp`) | Cross-language import/export |
| MIR JIT (`mir.c`) | Code generation and execution |
| Print/format (`print.cpp`) | Value formatting and display |
| Path resolution (`path.c`) | Module path resolution |

### 4.3 Reuse from LambdaPy (Pattern Clone)

The Python transpiler's architecture can be cloned and adapted for Ruby, since both
languages share similar semantics:

| Python component | Ruby adaptation | Shared % |
|-----------------|----------------|----------|
| `py_scope.cpp` → `rb_scope.cpp` | Add `RB_VAR_IVAR`/`RB_VAR_CVAR`/`RB_VAR_GVAR` kinds | ~70% |
| `py_class.cpp` → `rb_class.cpp` | Add modules/mixins, `attr_*` accessors | ~60% |
| `py_runtime.cpp` → `rb_runtime.cpp` | Adapt truthiness, division, operator dispatch | ~50% |
| `py_builtins.cpp` → `rb_builtins.cpp` | Map Ruby `Kernel`/`Enumerable` methods | ~40% |
| `transpile_py_mir.cpp` → `transpile_rb_mir.cpp` | Adapt for Ruby syntax (blocks, yield, etc.) | ~40% |

---

## 5. Cross-Language Module Import

### 5.1 Design Principle

All languages in Lambda share the same `Item` runtime data model and the same
`ModuleDescriptor` registry. Cross-language import is **transparent** — the caller
doesn't need to know what language the imported module was written in.

### 5.2 Ruby Import Syntax

```ruby
# Import a Lambda module
require_relative 'math_utils.ls'
result = math_utils.add(1, 2)

# Import a Python module
require_relative 'helpers.py'
data = helpers.process([1, 2, 3])

# Import a JavaScript module
require_relative 'validator.js'
valid = validator.check(input)

# Import another Ruby module
require_relative 'models.rb'
user = models.create_user("Alice")
```

### 5.3 Import Implementation

The transpiler handles `require_relative` by:

1. **Resolve path** — `path.c` resolves relative to current file's directory.
2. **Check registry** — `module_is_loaded(resolved_path)` for cached modules.
3. **Detect language** — file extension (`.ls`, `.py`, `.js`, `.rb`) determines parser.
4. **Compile + execute** — recursive call to language-specific transpiler.
5. **Register** — `module_register(path, "ruby", namespace_obj, mir_ctx)`.
6. **Bind** — the module name becomes a local variable holding the namespace `Item`.

```c
// In transpile_rb_mir.cpp — handling require_relative

void rb_emit_require_relative(RbMirTranspiler* mt, RbRequireNode* node) {
    const char* rel_path = node->path;  // e.g., "helpers.py"
    char resolved[PATH_MAX];
    resolve_relative_path(mt->current_file, rel_path, resolved, sizeof(resolved));

    // check if already loaded
    if (module_is_loaded(resolved)) {
        ModuleDescriptor* desc = module_get(resolved);
        // bind namespace to local variable
        rb_bind_module_var(mt, node->bind_name, desc->namespace_obj);
        return;
    }

    // detect language and compile
    const char* ext = get_file_extension(resolved);
    // reads file, invokes language-appropriate transpiler, registers module
    Item ns = compile_and_run_module(mt->runtime, resolved, ext);

    rb_bind_module_var(mt, node->bind_name, ns);
}
```

### 5.4 Ruby Module Exports

Ruby modules export functions via the namespace `Item` (a map):

```c
// After executing a Ruby script, build its namespace
Item rb_build_namespace(RbTranspiler* tp, void* mir_ctx) {
    Item ns = js_new_object();  // reuse js_new_object — it creates a Map

    // iterate over top-level method definitions
    for (RbMethodEntry* entry = tp->top_scope->methods; entry; entry = entry->next) {
        if (entry->is_public) {
            void* func_ptr = find_func((MIR_context_t)mir_ctx, entry->mir_name);
            if (func_ptr) {
                Function* fn = to_fn_named((fn_ptr)func_ptr, entry->arity, entry->name);
                Item key = {.item = s2it(heap_create_name(entry->name))};
                Item val = {.function = fn};
                js_property_set(ns, key, val);
            }
        }
    }

    module_register(tp->source_path, "ruby", ns, mir_ctx);
    return ns;
}
```

### 5.5 Calling Into Ruby from Other Languages

Other languages import Ruby modules via the same registry:

```python
# Python importing a Ruby module
import ruby_utils    # require_relative 'ruby_utils.rb' under the hood
result = ruby_utils.compute(42)
```

```javascript
// JavaScript importing a Ruby module
const utils = require('./ruby_utils.rb');
let result = utils.compute(42);
```

```
// Lambda importing a Ruby module
import ruby_utils from "ruby_utils.rb"
let result = ruby_utils.compute(42)
```

---

## 6. Implementation Phases

### Phase 1: Core Language (~5K LOC) — ✅ COMPLETE

**Goal**: Run basic Ruby programs — expressions, control flow, functions, print.

**Status**: Implemented and verified. 5,834 LOC across 8 source files. All baseline
regression tests pass (758/759 lambda, 32/32 radiant — 1 pre-existing test262 failure).

- [x] Set up `tree-sitter-ruby` grammar and build static library
- [x] Create `lambda/rb/` directory with initial files
- [x] `rb_ast.hpp` — AST node type enum and struct definitions (420 LOC)
- [x] `rb_transpiler.hpp` — transpiler context struct (112 LOC)
- [x] `build_rb_ast.cpp` — CST-to-AST builder for core expressions and statements (1,783 LOC)
- [x] `transpile_rb_mir.cpp` — MIR emission for core constructs (1,743 LOC)
- [x] `rb_runtime.cpp` — arithmetic, comparison, truthiness, `puts`/`p`/`print` (814 LOC)
- [x] `rb_scope.cpp` — scope management (local, global, constant) (230 LOC)
- [x] CLI integration in `main.cpp` — `./lambda.exe rb script.rb`
- [x] Build system: add `lambda/rb` to `build_lambda_config.json` source_dirs
- [x] Build system: add `tree-sitter-ruby` to libraries
- [x] Register `rb_*` runtime functions in `sys_func_registry.c` (60+ JitImport entries)
- [x] `rb_print.cpp` — Ruby-specific output formatting (596 LOC)
- [x] `rb_runtime.h` — C API header with extern "C" wrapping (136 LOC)

**Verified working in Phase 1:**

| Feature | Details | Status |
|---------|---------|--------|
| Literals | `42`, `3.14`, `"hello"`, `'world'`, `true`, `false`, `nil` | ✅ Verified |
| Arithmetic | `+`, `-`, `*`, `/`, `%`, `**` | ✅ Verified |
| Comparison | `==`, `!=`, `<`, `<=`, `>`, `>=` | ✅ Verified |
| Logical | `&&`, `||`, `!` | ✅ Verified |
| Variables | Local assignment, compound assignment (`+=`, `-=`, etc.) | ✅ Verified |
| Strings | Concatenation, conversion via `to_s` | ✅ Verified |
| Control flow | `if`/`elsif`/`else`/`end`, `unless`, `while` | ✅ Verified |
| If-as-expression | `result = if cond then a else b end` | ✅ Verified |
| Methods | `def`/`end`, implicit return, recursion (factorial) | ✅ Verified |
| Output | `puts`, `p`, `print` | ✅ Verified |
| Arrays | `[1, 2, 3]`, `.length`, subscript access, `.push` | ✅ Verified |
| Boolean ops | `&&`, `||`, `!` with Ruby truthiness (`0` is truthy) | ✅ Verified |
| Bitwise | `&`, `|`, `^`, `~`, `<<`, `>>` | ✅ Verified |
| Multiple assignment | `a, b = 1, 2` | ✅ Verified |
| String interpolation | `"#{expr}"` | ✅ Verified |
| Statement mods | `x if cond`, `x unless cond`, `x while cond` | ✅ Verified |
| Hashes | `{a: 1, "b" => 2}`, `[]` access | ✅ Verified |
| Ranges | `1..10`, `1...10` | ✅ Verified |
| `case`/`when` | With `===` case equality dispatch | ✅ Verified |
| `:symbol` literals | Symbols via name pool | ✅ Verified |
| `for..in` / `until` | Loop variants | ✅ Verified |
| Default params | `def foo(x, y=10)` | ✅ Verified |

### Phase 2: OOP & Blocks (~4K LOC)

**Goal**: Ruby classes, modules, blocks, iterators, closures.

- [ ] `rb_class.cpp` — class definition, instantiation, inheritance
- [ ] Instance variables (`@x`), class variables (`@@x`)
- [ ] `initialize` constructor → shaped instance creation
- [ ] Method dispatch: instance methods, class methods, `self`
- [ ] `attr_reader`, `attr_writer`, `attr_accessor`
- [ ] Single inheritance with `super` calls
- [ ] Blocks: `do..end` and `{ }` syntax, `yield`, `block_given?`
- [ ] `Proc.new`, `lambda`, `->(){}` stabby lambda
- [ ] `&block` parameter and `block.call`
- [ ] Closures: variable capture across nested scopes
- [ ] `include` for module mixins
- [ ] Iterator methods via blocks: `each`, `map`, `select`, `reject`, `reduce`, `collect`
- [ ] `Comparable` mixin support via `<=>`

**Supported in Phase 2:**

```ruby
class Animal
  attr_reader :name, :sound

  def initialize(name, sound)
    @name = name
    @sound = sound
  end

  def speak
    "#{@name} says #{@sound}"
  end
end

class Dog < Animal
  def initialize(name)
    super(name, "Woof")
  end

  def fetch(item)
    "#{@name} fetches the #{item}"
  end
end

dog = Dog.new("Rex")
puts dog.speak            # "Rex says Woof"
puts dog.fetch("ball")    # "Rex fetches the ball"

# Blocks and iterators
[1, 2, 3].map { |x| x * 2 }           # [2, 4, 6]
[1, 2, 3].select { |x| x > 1 }        # [2, 3]
[1, 2, 3].reduce(0) { |sum, x| sum + x }  # 6

# Custom iterator with yield
def repeat(n)
  n.times { |i| yield i }
end

repeat(3) { |i| puts i }
```

### Phase 3: Standard Library & Built-ins (~3K LOC)

**Goal**: Cover the commonly-used Ruby standard library methods.

- [ ] `rb_builtins.cpp` — `Kernel` methods
- [ ] `rb_stdlib.cpp` — `String`, `Array`, `Hash`, `Integer`, `Float`, `Enumerable` methods
- [ ] String methods: `upcase`, `downcase`, `strip`, `split`, `gsub`, `sub`, `scan`, `match`, `chars`, `bytes`, `tr`, `center`, `ljust`, `rjust`, `freeze`
- [ ] Array methods: `push`, `pop`, `shift`, `unshift`, `first`, `last`, `flatten`, `compact`, `uniq`, `sort`, `sort_by`, `min`, `max`, `sum`, `any?`, `all?`, `none?`, `count`, `zip`, `product`, `combination`, `permutation`, `rotate`, `sample`, `shuffle`, `take`, `drop`, `each_with_index`, `each_with_object`, `flat_map`, `chunk`, `group_by`, `tally`
- [ ] Hash methods: `each`, `map`, `select`, `reject`, `merge`, `fetch`, `key?`, `value?`, `delete`, `to_a`, `transform_keys`, `transform_values`, `slice`
- [ ] `Integer` methods: `times`, `upto`, `downto`, `even?`, `odd?`, `abs`, `gcd`, `lcm`, `digits`
- [ ] `Float` methods: `round`, `floor`, `ceil`, `truncate`, `nan?`, `infinite?`
- [ ] `Comparable` methods: `clamp`, `between?`
- [ ] Conversion: `to_i`, `to_f`, `to_s`, `to_a`, `to_h`, `to_sym`
- [ ] `Kernel` methods: `puts`, `p`, `print`, `gets`, `rand`, `srand`, `sleep`, `exit`, `raise`, `require_relative`, `open`, `sprintf`, `format`
- [ ] File I/O: `File.read`, `File.write`, `File.exist?`, `File.open` (block form)
- [ ] Regex: `Regexp.new`, `=~`, `match`, `scan`, `gsub`, `sub` (via RE2)
- [ ] Cross-language module imports via `require_relative`

### Phase 4: Error Handling & Advanced Features (~2K LOC)

**Goal**: Exception handling, advanced iterators, pattern matching.

- [ ] `begin`/`rescue`/`else`/`ensure`/`end`
- [ ] `raise` with string or exception class
- [ ] Custom exception classes inheriting `StandardError`
- [ ] `retry` within rescue blocks
- [ ] Inline rescue: `x = dangerous_op rescue default_value`
- [ ] `Enumerator` protocol: `each` + `Enumerator::Yielder`
- [ ] `Struct` — named struct creation
- [ ] `open` with block (auto-close)
- [ ] Heredocs (`<<~HEREDOC`)
- [ ] Multi-line strings
- [ ] `defined?` keyword
- [ ] `freeze` / `frozen?` (advisory only — no actual immutability enforcement)
- [ ] `respond_to?`
- [ ] `send` / `public_send` — dynamic dispatch
- [ ] `method_missing` (basic support)

### Phase 5: Performance Optimization (~1.5K LOC)

**Goal**: Apply the LambdaJS/LambdaPy optimization playbook — type inference + native
MIR emission for hot paths.

Following the proven 6-phase approach from Python (see `Transpile_Py6.md`):

- [ ] **P5a**: Type inference infrastructure — track `type_hint` on variables
- [ ] **P5b**: Native integer arithmetic — `MIR_ADD`/`MIR_SUB`/`MIR_MUL` for typed ints
- [ ] **P5c**: Native comparisons — `MIR_LTS`/`MIR_LES` for typed int/float
- [ ] **P5d**: Integer loop optimization — `n.times {}`, `for i in 0..n` → native counter
- [ ] **P5e**: Shaped slot property access — `@ivar` on known classes → O(1) indexed read
- [ ] **P5f**: Direct method dispatch — statically resolve method calls on known types

---

## 7. File Map

### New Files (in `lambda/rb/`)

| File | Est. LOC | Actual LOC | Status | Purpose |
|------|---------|-----------|--------|---------|
| File | Est. LOC | Actual LOC | Status | Purpose |
|------|---------|-----------|--------|---------|
| `rb_ast.hpp` | ~300 | 420 | ✅ Done | AST node types (70+), operator enums, struct definitions |
| `rb_transpiler.hpp` | ~120 | 112 | ✅ Done | Transpiler context struct, scope types, API declarations |
| `build_rb_ast.cpp` | ~2,500 | 1,783 | ✅ Done | Tree-sitter CST → Ruby AST conversion |
| `transpile_rb_mir.cpp` | ~6,000 | 1,743 | ✅ Phase 1 | AST → MIR IR emission (grows with later phases) |
| `rb_runtime.cpp` | ~2,000 | 814 | ✅ Phase 1 | Ruby operator dispatch, truthiness, type conversion |
| `rb_runtime.h` | ~100 | 136 | ✅ Done | Runtime function declarations (extern "C") |
| `rb_scope.cpp` | ~400 | 230 | ✅ Done | Scope management: local, global, constant |
| `rb_print.cpp` | ~200 | 596 | ✅ Done | `puts`, `p`, `print` with Ruby formatting semantics |
| `rb_class.cpp` | ~1,500 | — | Phase 2 | Class creation, inheritance, instance management, mixins |
| `rb_class.h` | ~50 | — | Phase 2 | Class support declarations |
| `rb_builtins.cpp` | ~800 | — | Phase 3 | Kernel/built-in function implementations |
| `rb_stdlib.cpp` | ~1,500 | — | Phase 3 | String/Array/Hash/Enumerable method implementations |
| **Phase 1 Total** | — | **5,834** | ✅ | |
| **Projected Total** | **~15,500** | | | |

### Modified Files

| File | Changes | Status |
|------|--------|--------|
| `build_lambda_config.json` | Add `lambda/rb` to `source_dirs`, add `tree-sitter-ruby` library | ✅ Done |
| `lambda/main.cpp` | Add `rb` CLI command, `#include "rb/rb_transpiler.hpp"` | ✅ Done |
| `lambda/sys_func_registry.c` | Register 60+ `rb_*` runtime functions for MIR import resolution | ✅ Done |
| `lambda/module_registry.cpp` | Add `"ruby"` as recognized `source_lang`; add `rb_build_namespace` | Phase 2 |
| `lambda/module_registry.h` | Declare `rb_build_namespace` | Phase 2 |

### New External Dependency

| Component | Source | Size |
|-----------|--------|------|
| `tree-sitter-ruby` | [github.com/tree-sitter/tree-sitter-ruby](https://github.com/tree-sitter/tree-sitter-ruby) | ~200–400KB `.a` |

Installed under `lambda/tree-sitter-ruby/` following the same layout as other
tree-sitter grammars (`grammar.js`, `src/parser.c`, `bindings/c/`, etc.).

---

## 8. Build System Changes

### `build_lambda_config.json` additions

```jsonc
{
    "source_dirs": [
        // ... existing ...
        "lambda/rb"                              // NEW
    ],
    "includes": [
        // ... existing ...
        "lambda/tree-sitter-ruby/bindings/c"     // NEW
    ],
    "libraries": [
        // ... existing ...
        {                                        // NEW
            "name": "tree-sitter-ruby",
            "include": "lambda/tree-sitter-ruby/bindings/c",
            "lib": "lambda/tree-sitter-ruby/libtree-sitter-ruby.a",
            "link": "static"
        }
    ]
}
```

After editing `build_lambda_config.json`, run `make` to regenerate Premake Lua and
Makefiles (per project convention — never manually edit `.lua` build files).

### Grammar Setup

```bash
# Clone tree-sitter-ruby into the project
cd lambda/tree-sitter-ruby
npm install        # pulls tree-sitter CLI
npx tree-sitter generate
make               # or: cc -c src/parser.c src/scanner.c -I src -o ...
ar rcs libtree-sitter-ruby.a parser.o scanner.o
```

---

## 9. Size Impact

### Binary Size Delta (release build)

| Component | Size |
|-----------|------|
| `libtree-sitter-ruby.a` | ~200–400KB |
| `rb_*.o` compiled objects | ~150–250KB |
| **Total delta** | **~350–650KB** (on ~8MB baseline) |

### Code Line Delta

| Metric | Estimated | Actual (Phase 1) |
|--------|----------|-----------------|
| New Ruby transpiler code | ~15,500 | 5,834 (8 files) |
| Modified existing code | ~200 | ~200 (3 files) |
| **Phase 1 LOC** | — | **~6,034** |
| **Projected Total** | **~15,700** | |

---

## 10. Risk Analysis

| Risk | Mitigation |
|------|-----------|
| Ruby's complex grammar (heredocs, regex literals, method_missing) | Phase the implementation: core first, edge cases later. Tree-sitter-ruby handles the hard parsing. |
| Block/proc/lambda semantics are nuanced (arity, return behavior) | Start with simple blocks; add `Proc` vs `lambda` distinction in Phase 2. Document unsupported edge cases. |
| Ruby's open classes and monkey-patching | Not supported in v1. All classes are closed after definition. Document as limitation. |
| Eval/binding/ObjectSpace — reflection-heavy code | Out of scope. These are fundamentally incompatible with JIT compilation. |
| Mutable strings (Ruby strings are mutable by default) | Use Lambda's immutable strings with copy-on-write semantics for mutating methods (`gsub!`, `<<`, etc.). |
| Large standard library surface area | Prioritize by usage frequency. Start with ~40 most common methods; expand based on test needs. |
| `method_missing` / dynamic method dispatch | Phase 4 feature — implement as fallback in method resolution chain. |
| Performance gap vs CRuby on startup | MIR JIT has near-zero startup. Hot-loop performance should exceed CRuby (~3-5× faster after Phase 5). |

---

## 11. Testing Strategy

### Unit Tests (`test/ruby/`)

```
test/ruby/
├── literals.rb          / literals.txt
├── arithmetic.rb        / arithmetic.txt
├── strings.rb           / strings.txt
├── arrays.rb            / arrays.txt
├── hashes.rb            / hashes.txt
├── control_flow.rb      / control_flow.txt
├── methods.rb           / methods.txt
├── classes.rb           / classes.txt
├── blocks.rb            / blocks.txt
├── closures.rb          / closures.txt
├── modules.rb           / modules.txt
├── iterators.rb         / iterators.txt
├── exceptions.rb        / exceptions.txt
├── ranges.rb            / ranges.txt
├── symbols.rb           / symbols.txt
├── regex.rb             / regex.txt
├── file_io.rb           / file_io.txt
├── cross_import_py.rb   / cross_import_py.txt     # imports .py module
├── cross_import_js.rb   / cross_import_js.txt     # imports .js module
├── cross_import_ls.rb   / cross_import_ls.txt     # imports .ls module
└── stdlib.rb            / stdlib.txt
```

Each `.rb` script has a corresponding `.txt` file with expected output (per project
convention — always add both when creating a new test).

### GTest C++ Tests (`test/test_ruby.cpp`)

```cpp
// Structured as per existing test_js.cpp and test_python.cpp patterns
TEST(RubyBaseline, Literals)    { run_ruby_test("literals"); }
TEST(RubyBaseline, Arithmetic)  { run_ruby_test("arithmetic"); }
TEST(RubyBaseline, Strings)     { run_ruby_test("strings"); }
// ...
```

### CI Integration

```bash
make test-ruby-baseline     # Must pass 100% before merge
```

### Benchmark Suite (Phase 5+)

Run the same multi-language benchmark suites (R7RS, AWFY, BENG, Kostya, Larceny) with
Ruby implementations to measure performance vs CRuby, LambdaJS, and LambdaPy.

---

## 12. Ruby Semantics: Key Differences from Python

Understanding where Ruby diverges from Python is essential for correct transpilation.
These cases require Ruby-specific runtime functions rather than reusing Python's:

| Aspect | Python | Ruby | Impact |
|--------|--------|------|--------|
| Integer division | `5 / 2 → 2` (floor div), `5 / 2.0 → 2.5` | `5 / 2 → 2` (truncate), `5 / 2.0 → 2.5` | `rb_div` truncates toward zero |
| Truthiness | Only `False`, `None`, `0`, `""`, `[]`, `{}` are falsy | Only `false` and `nil` are falsy (`0`, `""`, `[]` are truthy) | `rb_is_truthy` has simpler rules |
| String mutability | Immutable | Mutable (copy-on-write in Lambda) | `rb_string_mutate` creates copy |
| Block vs lambda scope | No equivalent | `return` in block returns from enclosing method | Different exception unwind |
| `==` vs `equal?` | `==` value, `is` identity | `==` value, `equal?` identity | Same concept, different names |
| Symbols vs strings | No symbols (use strings) | `:symbol` is distinct optimized type | Direct map to Lambda symbols |
| Open classes | No monkey-patching | Classes can be reopened | Not supported in v1 |
| Method visibility | Conventional `_private` | `public`/`private`/`protected` keywords | Enforced at dispatch time |
| Block iteration | Generators (`yield` creates generator) | Blocks (`yield` calls block inline) | Fundamentally different `yield` |

---

## 13. Milestone Checkpoints

| Milestone | Criteria | Est. LOC | Status |
|-----------|---------|--------|--------|
| **M1: Hello World** | `./lambda.exe rb script.rb` prints output | ~2K | ✅ Done |
| **M2: Core Language** | Expressions, control flow, methods, arrays — Phase 1 complete | ~5K | ✅ Done (5,834 LOC) |
| **M3: OOP** | Classes, inheritance, blocks, iterators — Phase 2 complete | ~9K | Not started |
| **M4: Standard Library** | 40+ built-in methods, regex, file I/O — Phase 3 complete | ~12K | Not started |
| **M5: Error Handling** | begin/rescue/ensure, custom exceptions — Phase 4 complete | ~14K | Not started |
| **M6: Cross-Language** | `require_relative` imports .ls/.py/.js modules; verified bidirectional | ~14.5K | Not started |
| **M7: Performance** | Type inference + native MIR emission; benchmark suite — Phase 5 complete | ~15.5K | Not started |
| **M8: Baseline Tests** | 100% pass rate on `test-ruby-baseline` (60+ tests) | ~15.5K | Not started |

---

## 14. Out of Scope (v1)

The following Ruby features are explicitly **not targeted** for the initial implementation:

| Feature | Reason |
|---------|--------|
| `eval`, `binding`, `instance_eval`, `class_eval` | Incompatible with ahead-of-time MIR compilation |
| `ObjectSpace`, `GC` module | Internal GC is Lambda's — not exposed to Ruby |
| `Thread`, `Fiber`, `Ractor` | No concurrency model in Lambda runtime (yet) |
| `require` (gem loading) | Only `require_relative` for local files; no RubyGems |
| C extensions / FFI | Lambda provides its own native function mechanism |
| Open classes / monkey-patching | Classes are closed after definition |
| `method_missing` (Phase 4 stretch) | Complex to implement correctly with JIT; deferred |
| Refinements | Scoped monkey-patching — unnecessary with closed classes |
| `Encoding` / multi-encoding strings | Lambda uses UTF-8 throughout |
| `TracePoint`, `set_trace_func` | No debugging hooks in JIT mode |
| Frozen string literals pragma | All Lambda strings are effectively immutable |
| `Comparable`, `Enumerable` as full mixins | Individual methods implemented directly instead |

---

## 15. Implementation Notes (Phase 1)

Key decisions and lessons from the Phase 1 implementation:

### MIR Naming Conventions

- **Runtime functions** use `rb_` prefix (e.g., `rb_add`, `rb_sub`, `rb_eq`) — registered
  as MIR imports in `sys_func_registry.c`.
- **User-defined functions** use `rbu_` prefix (e.g., `rbu_factorial`, `rbu_add`) to avoid
  MIR name collisions with runtime imports.
- **MIR call protos** use a counter suffix `call_%s_%d_p` for uniqueness, preventing
  "already defined as proto" errors in recursive or multi-call functions.

### Linkage

- `rb_runtime.cpp` functions are declared `extern "C"` (via `rb_runtime.h`).
- Use `format_item()` (which is `extern "C"` in `ast.hpp`) for value printing — not
  `print_root_item()` which has C++ linkage only.
- C++ inline accessors (`it2arr`, `it2range`, `it2map`) from `lambda.hpp` are used
  instead of C macros from `lambda.h` (which are `#ifndef __cplusplus` guarded).

### Ruby-Specific Semantics Implemented

- **If-as-expression**: Ruby's `if`/`elsif`/`else` returns a value. Implemented via
  `rm_transpile_if_expr()` which captures the last expression from each branch into a
  result register.
- **Truthiness**: Only `false` and `nil` are falsy — `0`, `""`, `[]` are all truthy
  (handled by `rb_is_truthy` in runtime).
- **Implicit return**: Functions return the value of their last expression.

### Type Mappings Verified

| Ruby | Lambda Item | Accessor |
|------|------------|---------|
| Integer | `LMD_TYPE_INT` | `it2i()` → `int64_t` (56-bit inline) |
| Float | `LMD_TYPE_FLOAT` | `it2f()` → `double` |
| String | `LMD_TYPE_STRING` | `it2s()` → `const char*` |
| Boolean | `LMD_TYPE_BOOL` | `it2b()` → `bool` |
| Nil | `LMD_TYPE_NULL` | `ItemNull` constant |
| Array | `LMD_TYPE_ARRAY` | `it2arr()` → `Array*` |
| Range | `LMD_TYPE_RANGE` | `it2range()` → `Range*` (start/end/length, no exclusive field) |

### Known Limitations (Phase 1)

- Method calls on objects (e.g., `"ruby".upcase`) only support `.length`/`.size`,
  `.push`, `.to_s`, `.to_i`, `.to_f` — other methods return nil.
- No string interpolation (`"#{expr}"`) — use concatenation with `to_s` instead.
- No hash literal support yet.
- No `case`/`when`, `for..in`, `until` loops.
- No multiple assignment (`a, b = 1, 2`).
- No symbol (`:name`) literals.
- No block/proc/lambda support.
- No class definitions.
