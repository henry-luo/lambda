# LambdaPy — Python Transpiler Proposal

## Overview

This document proposes adding Python 3 support to the Lambda runtime, following the same architecture used by LambdaJS. Python source files (`.py`) would be compiled to native machine code through the Lambda JIT pipeline: Tree-sitter parsing → typed AST → MIR IR → native execution. All Python values would be represented as Lambda `Item` values, enabling zero-cost interop with Lambda's existing infrastructure (GC, name pool, shape system, input parsers, output formatters).

> **Implementation Status (audited 2026-03-26):** The core of this proposal is **implemented and working** (~10,865 LOC). Phases 1–2 and 4–5 from Appendix B are fully complete. Phase 3 (classes), Phase 6 (generators), and Phase 7 (standard library modules) are explicitly deferred to v3+.
>
> | Area | Status |
> |------|--------|
> | Architecture / pipeline | ✅ Complete |
> | AST builder (`build_py_ast.cpp`) | ✅ Complete — all node types parsed |
> | Core transpiler — expressions, control flow, functions | ✅ Complete |
> | LEGB scoping, `global`/`nonlocal` | ✅ Complete |
> | Closures, cell variables | ✅ Complete |
> | Comprehensions (list/dict/set), lambda | ✅ Complete |
> | Iterators (`iter`/`next`/`range`/`enumerate`/`zip`) | ✅ Complete |
> | Exception handling (`try`/`except`/`finally`/`raise`) | ✅ Complete (type matching + exception hierarchy strings) |
> | String/list/dict methods (30+/11/10) | ✅ Complete |
> | 40+ built-in functions | ✅ Complete |
> | `*args` variadic parameters | ✅ Complete |
> | F-strings with format specs, `%` formatting, `str.format()` | ✅ Complete |
> | Slice read + assignment | ✅ Complete |
> | Build integration, CLI `./lambda.exe py` | ✅ Complete |
> | **Class definitions** (`class`, MRO, `super()`) | ❌ Deferred to v3 |
> | **`with` statement** (context managers) | ❌ Deferred |
> | **Generators / `yield`** | ❌ Deferred to v4+ |
> | **`**kwargs`** | ❌ Not yet implemented |
> | **`@` matmul operator** | ❌ Not needed yet |
> | **Standard library modules** (`json`, `math`, `re`, etc.) | ❌ Deferred to v3+ |

### Design Goals

1. **Unified runtime** — Python values are Lambda `Item` values. No conversion boundary.
2. **Reuse over reimplementation** — Delegate to Lambda subsystems for JSON, regex, string interning, memory management, formatting.
3. **Near-native performance** — Type inference + native arithmetic fast paths (same strategy as JS).
4. **Small parser footprint** — Use a stripped-down tree-sitter-python grammar to minimize binary size.
5. **Pragmatic subset first** — Target the most-used Python features; defer rarely-used features to later phases.

### Non-Goals (Initial Release)

- Full CPython standard library compatibility
- C extension module loading (`import numpy`)
- `async`/`await` and asyncio event loop
- Metaclasses and advanced descriptor protocol
- `exec()` / `eval()` of dynamic Python code

---

## 1. Architecture

### 1.1 Compilation Pipeline

```
Python Source (.py)
    │
    ▼
Tree-sitter Parser     (tree-sitter-python grammar, stripped)
    │
    ▼
Py AST Builder         (build_py_ast.cpp → typed PyAstNode tree)
    │
    ▼
MIR Transpiler         (transpile_py_mir.cpp → MIR IR instructions)
    │
    ▼
MIR JIT Compiler       (MIR_link + MIR_gen → native machine code)
    │
    ▼
Execution              (py_main() function pointer call → Item result)
```

### 1.2 Unified Runtime Architecture

```
┌──────────────────────────────────────────────────────────────────┐
│                      CLI (main.cpp)                              │
│         ./lambda.exe py script.py                                │
├──────────────┬──────────────────┬────────────────────────────────┤
│  Lambda Path │  JavaScript Path │          Python Path           │
│  (.ls files) │  (.js files)     │          (.py files)           │
│              │                  │                                │
│  build_ast   │  build_js_ast    │  build_py_ast.cpp              │
│  transpile   │  transpile_js_mir│  transpile_py_mir.cpp          │
│              │                  │                                │
├──────────────┴──────────────────┴────────────────────────────────┤
│                  Shared Runtime Infrastructure                    │
│                                                                   │
│  ┌────────────┐ ┌──────────┐ ┌─────────────┐ ┌───────────────┐  │
│  │ Item Type  │ │ GC Heap  │ │  MIR JIT    │ │ import_resolver│  │
│  │ System     │ │ & Nursery│ │ (mir.c)     │ │ (sys_func_    │  │
│  │ (TypeId)   │ │          │ │             │ │  registry.c)  │  │
│  └────────────┘ └──────────┘ └─────────────┘ └───────────────┘  │
│  ┌────────────┐ ┌──────────┐ ┌─────────────┐ ┌───────────────┐  │
│  │ Name Pool  │ │ Memory   │ │ Shape System│ │ I/O Parsers & │  │
│  │ (interning)│ │ Pool     │ │ (ShapeEntry)│ │ Formatters    │  │
│  └────────────┘ └──────────┘ └─────────────┘ └───────────────┘  │
└──────────────────────────────────────────────────────────────────┘
```

**Key insight:** Like JS, the Python path bypasses Lambda's `runner.cpp`. The CLI detects `.py` files and calls `transpile_py_to_mir()` directly.

### 1.3 Execution Entry Point

```
main.cpp                           transpile_py_mir.cpp
────────                           ────────────────────
argv[1] == "py"
  │
  ├─ runtime_init(&runtime)        // shared Runtime struct
  ├─ lambda_stack_init()           // GC stack bounds
  ├─ read_text_file(py_file)       // read Python source
  │
  └─ transpile_py_to_mir()  ──────→  py_transpiler_create()
                                      py_transpiler_parse()        // TS parse
                                      build_py_ast()               // TS CST → Py AST
                                      jit_init(2)                  // MIR ctx
                                      transpile_py_mir_ast()       // AST → MIR IR
                                      MIR_link(import_resolver)    // link imports
                                      find_func(ctx, "py_main")   // locate entry
                                      py_main(&eval_context)       // execute
                                      ← return Item result
```

---

## 2. Tree-sitter Python Grammar — Minimizing Parser Size

### 2.1 The Problem

The stock `tree-sitter-python` grammar generates a `parser.c` of ~230K lines (~6.5 MB source, ~800 KB static lib after compilation). This is 3× the size of tree-sitter-javascript (84K lines, 391 KB lib) because Python's grammar has many states for indentation handling and extensive sugar syntax.

### 2.2 Strategy: Use the Stock Grammar, Strip Unused Rules

Rather than forking and modifying `grammar.js` (which complicates future grammar updates), we use the stock tree-sitter-python grammar but accept the parsing overhead. The AST builder will simply ignore node types we don't support yet and emit a clear error.

**Rationale:**
- Forking `grammar.js` creates a maintenance burden — upstream Python syntax changes require manual merging.
- The parser.c size affects source repo size, not meaningfully affect runtime. The compiled `.a` is the relevant metric.
- Unsupported syntax gets a clean error at AST building time rather than a confusing parse error.

### 2.3 Estimated parser size

| Component | tree-sitter-javascript | tree-sitter-python (stock) |
|-----------|----------------------|---------------------------|
| `parser.c` | 84K lines / 2.4 MB | ~230K lines / 6.5 MB |
| `scanner.c` | 364 lines | ~2K lines (indent/dedent handling) |
| Compiled `.a` | 391 KB | ~700–800 KB |
| Binary contribution | ~300 KB | ~500–600 KB |

### 2.4 Alternative: Build a Reduced Grammar

If the binary size is a concern, we could build a stripped grammar that removes:
- Pattern matching (`match`/`case`) — saves ~15K states
- `async`/`await` productions — saves ~8K states  
- Type annotation syntax — saves ~10K states (type hints are ignored at runtime anyway)
- Walrus operator (`:=`) — saves ~2K states

This could bring `parser.c` down to ~150K lines and the `.a` to ~500 KB. The tradeoff is forking the grammar and maintaining patches.

**Recommendation:** Start with the stock grammar. Optimize only if binary size becomes a problem.

> **Issue [G1]:** The tree-sitter-python scanner handles `INDENT`/`DEDENT` tokens via an external scanner written in C. This scanner maintains indentation stacks and is well-tested. We must bundle it unmodified — Python's significant whitespace cannot be reasonably handled by the grammar alone.

---

## 3. Python Types ↔ Lambda Runtime Types

### 3.1 Type Mapping Table

| Python Type | Lambda TypeId | Representation | Boxing |
|-------------|---------------|----------------|--------|
| `int` (small) | `LMD_TYPE_INT` | Tag bits + 56-bit signed value inline | Inline (no allocation) |
| `int` (large) | `LMD_TYPE_INT64` | GC nursery-allocated `int64_t*` | Pointer to nursery |
| `float` | `LMD_TYPE_FLOAT` | GC nursery-allocated `double*` | Pointer to nursery |
| `str` | `LMD_TYPE_STRING` | `String*` via `heap_create_name()` | GC heap allocated |
| `bool` | `LMD_TYPE_BOOL` | Tag bits + 0/1 inline | Inline (no allocation) |
| `None` | `LMD_TYPE_NULL` | Tag-only sentinel | Inline |
| `list` | `LMD_TYPE_ARRAY` | Lambda `Array` (dynamic `Item*` buffer) | GC heap allocated |
| `dict` | `LMD_TYPE_MAP` | Lambda `Map` with `ShapeEntry` field chain | GC heap allocated |
| `tuple` | `LMD_TYPE_ARRAY` | Lambda `Array` with immutable flag | GC heap allocated |
| `set` / `frozenset` | `LMD_TYPE_MAP` | Map with sentinel marker (keys only) | GC heap allocated |
| `function` | `LMD_TYPE_FUNC` | Pool-allocated `PyFunction` struct | Pool allocated |
| `class instance` | `LMD_TYPE_MAP` | Map with `__class__` field + sentinel marker | GC heap allocated |
| `bytes` / `bytearray` | `LMD_TYPE_BINARY` | Lambda Binary (raw byte buffer) | GC heap allocated |
| `range` | `LMD_TYPE_RANGE` | Lambda `Range` (start, end, step) | GC heap allocated |
| `complex` | `LMD_TYPE_MAP` | Map with `.real` and `.imag` float fields | GC heap allocated |

### 3.2 Key Type Design Decisions

**Python `int` is arbitrary-precision — Lambda `int` is not.**
Lambda's `LMD_TYPE_INT` holds 56-bit signed values inline; `LMD_TYPE_INT64` extends to 64-bit. Python mandates arbitrary-precision integers (`10 ** 100` is valid). Options:

| Approach | Pros | Cons |
|----------|------|------|
| **(A) Cap at int64, error on overflow** | Simple, fast, no new types | Not Python-compliant; breaks code relying on big ints |
| **(B) Overflow to decimal** | Reuses `LMD_TYPE_DECIMAL` (existing) | Decimal has different semantics (fixed-point, not integer) |
| **(C) Add LMD_TYPE_BIGINT** | Full Python semantics | New type, adds ~500 LOC for bigint arithmetic |

> **Suggestion [T1]:** Start with approach (A) — cap at int64, log a warning on overflow. Most practical Python scripts stay within 64-bit range. Add big-int support as a later enhancement if needed.

**Python `tuple` vs `list`.**
Both map to `LMD_TYPE_ARRAY`. Tuples must be immutable. We can distinguish them via a sentinel marker on the Array struct (same pattern JS uses for TypedArrays vs regular arrays) or with a flag bit on the container header.

> **Suggestion [T2]:** Add a `flags` field or use a sentinel pointer on `Array` to mark immutability. This also benefits Lambda's own frozen collections.

**Python `dict` ordering.**
Since Python 3.7, dicts maintain insertion order. Lambda's `Map` with linked `ShapeEntry` chains already preserves insertion order. This is a natural fit.

**Python `None` ↔ Lambda `null`.**
Direct 1:1 mapping. No special handling needed (unlike JS which required a separate `undefined` type).

### 3.3 Reuse of Lambda System Functions

| Capability | Lambda Subsystem | Python API Surface |
|-----------|-----------------|-------------------|
| JSON parsing | `input-json.cpp` → `parse_json_to_item()` | `json.loads(str)` |
| JSON output | `format/format-json.cpp` → `format_json()` | `json.dumps(value)` |
| Regex (RE2) | `re2_wrapper.hpp` | `re.match()`, `re.search()`, `re.findall()`, `re.sub()` |
| String interning | `name_pool.hpp` → `name_pool_get()` | Attribute name lookups, dict key optimization |
| Shape system | `shape_builder.hpp` | Object attribute layout |
| GC heap | `lambda-mem.cpp` → `heap_alloc()`, `gc_nursery` | All object/string allocation |
| Memory pools | `lib/mempool.h` → `pool_create()` | Temporary allocations, closures |
| Format output | `format/format.h` | `print()` output formatting |
| Math functions | Lambda math sys_funcs | `math.sqrt`, `math.sin`, etc. |

---

## 4. Python AST Design

### 4.1 AST Node Types (`py_ast.hpp`)

```c
typedef enum PyAstNodeType {
    // program
    PY_AST_MODULE,

    // statements
    PY_AST_EXPRESSION_STMT,
    PY_AST_ASSIGNMENT,
    PY_AST_AUGMENTED_ASSIGNMENT,   // +=, -=, *=, etc.
    PY_AST_ANNOTATED_ASSIGNMENT,   // x: int = 5 (annotation ignored at runtime)
    PY_AST_RETURN,
    PY_AST_IF,
    PY_AST_ELIF,
    PY_AST_ELSE,
    PY_AST_WHILE,
    PY_AST_FOR,                    // for x in iterable
    PY_AST_BREAK,
    PY_AST_CONTINUE,
    PY_AST_PASS,
    PY_AST_FUNCTION_DEF,
    PY_AST_CLASS_DEF,
    PY_AST_IMPORT,                 // import module
    PY_AST_IMPORT_FROM,            // from module import name
    PY_AST_GLOBAL,
    PY_AST_NONLOCAL,
    PY_AST_DEL,
    PY_AST_ASSERT,
    PY_AST_RAISE,
    PY_AST_TRY,
    PY_AST_EXCEPT,
    PY_AST_FINALLY,
    PY_AST_WITH,
    PY_AST_YIELD,
    PY_AST_YIELD_FROM,

    // expressions
    PY_AST_IDENTIFIER,
    PY_AST_LITERAL,                // int, float, string, bool, None
    PY_AST_FSTRING,                // f"hello {name}"
    PY_AST_BINARY_OP,
    PY_AST_UNARY_OP,
    PY_AST_BOOLEAN_OP,             // and, or
    PY_AST_COMPARE,                // chained: a < b < c
    PY_AST_CALL,
    PY_AST_ATTRIBUTE,              // obj.attr
    PY_AST_SUBSCRIPT,              // obj[key]
    PY_AST_SLICE,                  // [start:stop:step]
    PY_AST_STARRED,                // *args
    PY_AST_LIST,
    PY_AST_TUPLE,
    PY_AST_DICT,
    PY_AST_SET,
    PY_AST_LIST_COMPREHENSION,
    PY_AST_DICT_COMPREHENSION,
    PY_AST_SET_COMPREHENSION,
    PY_AST_GENERATOR_EXPRESSION,
    PY_AST_CONDITIONAL_EXPR,       // x if cond else y
    PY_AST_LAMBDA,
    PY_AST_NOT,
    PY_AST_KEYWORD_ARGUMENT,       // func(key=value)
    PY_AST_WALRUS,                 // :=

    // destructuring / unpacking
    PY_AST_TUPLE_UNPACK,           // a, b = ...
    PY_AST_LIST_UNPACK,            // [a, b] = ...
    PY_AST_STAR_UNPACK,            // a, *rest = ...

    // Python-specific operators encoded in BinaryOp
    // //, **, @, in, not in, is, is not

    PY_AST_NODE_COUNT
} PyAstNodeType;

typedef enum PyOperator {
    PY_OP_ADD, PY_OP_SUB, PY_OP_MUL, PY_OP_DIV,
    PY_OP_FLOOR_DIV,      // //
    PY_OP_MOD, PY_OP_POW, // **, note: right-associative
    PY_OP_MATMUL,          // @
    PY_OP_LSHIFT, PY_OP_RSHIFT,
    PY_OP_BIT_AND, PY_OP_BIT_OR, PY_OP_BIT_XOR, PY_OP_BIT_NOT,
    PY_OP_EQ, PY_OP_NE, PY_OP_LT, PY_OP_LE, PY_OP_GT, PY_OP_GE,
    PY_OP_AND, PY_OP_OR, PY_OP_NOT,
    PY_OP_IN, PY_OP_NOT_IN,
    PY_OP_IS, PY_OP_IS_NOT,
    PY_OP_NEGATE,          // unary -
    PY_OP_POSITIVE,        // unary +
    PY_OP_COUNT
} PyOperator;
```

### 4.2 Key Structural Differences from JS AST

| Feature | JS AST | Python AST |
|---------|--------|------------|
| Chained comparison | Not possible (`a < b < c` is `(a < b) < c`) | `PY_AST_COMPARE` with chain of ops+operands |
| Comprehensions | N/A | `PY_AST_LIST_COMPREHENSION` with nested `for`/`if` clauses |
| Decorators | N/A (class methods only) | `PY_AST_FUNCTION_DEF` and `PY_AST_CLASS_DEF` each carry a decorator list |
| Multiple assignment targets | `let [a, b] = [1, 2]` (destructuring) | `a, b = 1, 2` (tuple unpacking), also `a = b = 1` (multi-target) |
| `*args` / `**kwargs` | Spread in calls only | Both in function def params and call arguments |
| `with` statement | N/A | `PY_AST_WITH` — context manager protocol |
| `global` / `nonlocal` | N/A | Explicit scope directives — affect variable resolution |
| f-strings | Template literals (similar) | `PY_AST_FSTRING` — interpolated expressions in strings |
| Walrus operator | N/A | `:=` assignment as expression |

---

## 5. Transpiler Design

### 5.1 Transpiler Context (`py_transpiler.hpp`)

```c
typedef enum PyVarKind {
    PY_VAR_LOCAL,       // assigned in current scope
    PY_VAR_GLOBAL,      // declared with `global`
    PY_VAR_NONLOCAL,    // declared with `nonlocal`
    PY_VAR_FREE,        // captured from enclosing scope (closure)
    PY_VAR_CELL,        // local but captured by inner function
    PY_VAR_MODULE,      // top-level module variable
} PyVarKind;

typedef enum PyScopeType {
    PY_SCOPE_MODULE,
    PY_SCOPE_FUNCTION,
    PY_SCOPE_CLASS,
    PY_SCOPE_COMPREHENSION,   // comprehensions have their own scope in Python 3
} PyScopeType;

typedef struct PyScope {
    PyScopeType type;
    struct PyScope* parent;
    // variable entries...
} PyScope;

typedef struct PyTranspiler {
    Pool* ast_pool;
    Pool* name_pool;
    StrBuf* code_buf;
    StrBuf* error_buf;
    PyScope* current_scope;
    PyScope* module_scope;
    TSParser* parser;
    TSTree* tree;
    Runtime* runtime;
    int function_counter;
    int temp_var_counter;
} PyTranspiler;
```

### 5.2 Multi-Phase Compilation (Mirroring JS)

| Phase | Name | Description | Status |
|-------|------|-------------|--------|
| 1.0 | Function & Class Collection | Walk AST, collect all `def` and `class` declarations | ✅ Done |
| 1.1 | Constant Folding | Literal `const` assignments; fold constant expressions | ✅ Done |
| 1.2 | Scope Analysis | Classify every name as local/global/nonlocal/free/cell using Python's LEGB rules | ✅ Done |
| 1.3 | Module Variables | Top-level assignments → module variable indices | ✅ Done |
| 1.5 | Capture Analysis | Identify free variables; determine cell variables (locals captured by inner funcs) | ✅ Done |
| 1.6 | Transitive Propagation | Multi-level closure captures propagated | ✅ Done |
| 1.7 | Scope Env Computation | Parent functions get shared cell env arrays | ✅ Done |
| 1.75 | Type Inference | Arithmetic patterns → int/float typing for native fast paths | ✅ Done |
| 1.9 | Forward Declarations | MIR forward refs for all functions | ✅ Done |
| 2 | Code Generation | Emit MIR for each function body | ✅ Done |
| 3 | Entry Point | `py_main()` creation — top-level module statements | ✅ Done |

### 5.3 Python-Specific Compilation Challenges

#### 5.3.1 LEGB Scoping + `global`/`nonlocal` ✅ DONE

Python's scope resolution is fundamentally different from JavaScript's:
- JS resolves variables lexically at parse time (`var`/`let`/`const` declarations are explicit).
- Python resolves variables by **assignment presence**: if a name is assigned anywhere in a function, it's local to that function — even if the assignment comes after a read.

```python
x = 10
def foo():
    print(x)   # UnboundLocalError! x is local because of the assignment below
    x = 20
```

**Implementation:** Phase 1.2 must do a full pre-pass over each function body to find all assignment targets before code generation. Variables with `global` or `nonlocal` declarations override this analysis.

> **Issue [S1]:** This pre-pass is unique to Python and doesn't exist in the JS transpiler. A two-pass approach per function (scan assignments first, then generate code) adds ~300–500 lines to the transpiler.

#### 5.3.2 Chained Comparisons ✅ DONE

Python's `a < b < c` means `a < b and b < c` with `b` evaluated only once. This expands to:

```
MIR:   reg_a = <eval a>
       reg_b = <eval b>
       reg_cmp1 = call py_compare(reg_a, reg_b, PY_OP_LT)
       bf reg_cmp1, label_end      // short-circuit
       reg_c = <eval c>
       reg_cmp2 = call py_compare(reg_b, reg_c, PY_OP_LT)
       label_end:
       // result = reg_cmp2 (or False if short-circuited)
```

#### 5.3.3 Comprehensions ✅ DONE

List/dict/set comprehensions and generator expressions each create their own scope (Python 3 semantics). They are compiled as inline anonymous functions:

```python
squares = [x*x for x in range(10) if x % 2 == 0]
```

Transpiles to an inline loop that builds an `Array`:
```
reg_result = call py_list_new()
for x in range(10):
    if x % 2 == 0:
        call py_list_append(reg_result, py_multiply(x, x))
```

#### 5.3.4 Iterator Protocol ✅ DONE

Python's `for` loop operates on the iterator protocol (`__iter__` / `__next__`). The transpiler must emit:

```
reg_iter = call py_get_iterator(iterable)
label_loop:
  reg_val = call py_iterator_next(reg_iter)
  bt reg_val == STOP_ITERATION, label_end
  // loop body with reg_val
  jmp label_loop
label_end:
```

This requires a runtime sentinel (`PY_STOP_ITERATION`) distinct from `None`.

> **Suggestion [T3]:** Use `ItemError` with a specific error code as the StopIteration sentinel, or add a small flag on the iterator struct. Do *not* add a new `TypeId` for this — it is control flow, not a data type.

#### 5.3.5 `*args` and `**kwargs` — `*args` ✅ DONE, `**kwargs` ❌ Not yet

Function definitions with variadic parameters:
```python
def foo(a, b, *args, key=None, **kwargs): ...
```

The transpiler must pack excess positional arguments into an Array (`*args`) and excess keyword arguments into a Map (`**kwargs`). At call sites, `*iterable` unpacks into positional args and `**mapping` unpacks into keyword args.

> **Issue [S2]:** Keyword argument passing is a major architectural difference from JS. JS only has positional arguments; Python has positional, keyword, default values, `*args`, `**kwargs`, keyword-only, and positional-only parameters. The call convention must be flexible. Options:
>
> **(A) All calls pass `(positional_array, keyword_map)`** — simple but adds overhead to every call.
> **(B) Compile-time resolution for known call targets** — when the callee signature is known, emit direct positional MIR calls (fast path) and fall back to (A) for dynamic calls.
>
> **Recommendation:** Approach (B). For statically-known calls (majority), emit direct MIR calls matching the parameter order, with default values filled in at the call site. Only fall back to the array+map convention for `*args`/`**kwargs` or dynamic dispatch.

#### 5.3.6 Class Model ❌ Deferred to v3

Python classes use C3 linearization (MRO) for method resolution with multiple inheritance. This is fundamentally different from JavaScript's single-prototype chain.

> **Status:** AST node `PY_AST_NODE_CLASS_DEF` is parsed by `build_py_ast.cpp`. The transpiler has no code generation for class bodies yet — classes are deferred to v3. No `py_class_new`, `py_compute_mro`, or `py_super` functions exist in the runtime.

```python
class Animal:
    def __init__(self, name):
        self.name = name
    def speak(self):
        return f"{self.name} speaks"

class Dog(Animal):
    def speak(self):
        return f"{self.name} barks"
```

**Implementation:**
- Each class is compiled to a `PyClassObject` (a Map with sentinel marker) containing:
  - `__name__`: class name string
  - `__bases__`: tuple of base classes  
  - `__mro__`: pre-computed Method Resolution Order (C3 linearization)
  - `__dict__`: Map of methods and class attributes
- Instance creation: `py_new_instance(class_obj)` → allocates a Map with `__class__` pointing to the class object.
- Method resolution: `py_getattr(instance, name)` → walks `__mro__` checking each class's `__dict__`.
- `super()`: Uses `__mro__` to find the next class in line.

> **Issue [S3]:** C3 MRO computation adds ~200 lines. Multiple inheritance method resolution at runtime is slower than JS's single-prototype-chain lookup. For single-inheritance classes (the common case), we can fast-path to direct `__dict__` lookup on the single base class.

#### 5.3.7 Exception Handling ✅ DONE (basic mechanism + type matching; class-based hierarchy deferred)

Python exceptions are class-based with inheritance. `except ValueError as e:` must check if the exception is an instance of `ValueError` or any subclass.

> **Status:** Global exception state, `raise`, `try`/`except`/`finally`, and type-name matching (`except ValueError:`) are fully implemented. Runtime ops like `py_divide` set `py_exception_value` to the type name string (e.g. `"ZeroDivisionError"`); `py_exception_get_type()` handles both string and Map-based exception objects. Full class-based exception hierarchy with `isinstance` via MRO is deferred to v3.

**Implementation:** Same global-exception-state approach as JS:
```c
static bool py_exception_pending;
static Item py_exception_value;      // the exception instance
static Item py_exception_type;       // the exception class
```

- `raise ExcType(msg)` → `py_raise(exc_instance)` sets pending exception
- `except ExcType as e:` → `py_check_exception_type(ExcType)` tests `isinstance` using MRO
- `finally:` → same deferred-return pattern as JS
- Built-in exception hierarchy (TypeError, ValueError, KeyError, IndexError, etc.) created as class objects at runtime init

#### 5.3.8 Context Managers (`with` statement) ❌ Deferred

```python
with open(file) as f:
    data = f.read()
```

> **Status:** `PY_AST_NODE_WITH` is parsed by `build_py_ast.cpp`. The transpiler has no code generation for `with` blocks. `py_context_enter`/`py_context_exit` runtime functions are not yet implemented. Deferred — `open()` is available as a builtin for reading entire files, but not as a context manager.

Compiles to:
```
reg_mgr = <eval expression>
reg_val = call py_context_enter(reg_mgr)       // calls __enter__
// body using reg_val
call py_context_exit(reg_mgr, exc_info)          // calls __exit__
```

This requires the transpiler to emit try/finally around the body.

---

## 6. Python Runtime Library Design

### 6.1 File Layout (Actual vs Estimated)

| File | Est. LOC | Actual LOC | Status | Notes |
|------|----------|-----------|--------|-------|
| `transpile_py_mir.cpp` | ~13,000 | **4,283** | ✅ Core done | Classes/with/generators not yet generated |
| `py_runtime.cpp` | ~3,500 | **1,587** | ✅ Core done | No MRO/class/context-manager runtime |
| `py_builtins.cpp` | ~2,000 | **1,633** | ✅ Done | Includes string/list/dict methods (merged) |
| `py_class.cpp` | ~1,200 | **—** | ❌ Deferred | Merged future plan; class system deferred to v3 |
| `py_string.cpp` | ~800 | **—** | ✅ (merged) | String methods live in `py_builtins.cpp` |
| `py_collections.cpp` | ~700 | **—** | ✅ (merged) | List/dict methods live in `py_builtins.cpp` |
| `py_iterator.cpp` | ~600 | **—** | ✅ (merged) | Iterator protocol lives in `py_runtime.cpp` |
| `build_py_ast.cpp` | ~2,500 | **1,937** | ✅ Done | All node types built |
| `py_ast.hpp` | ~500 | **462** | ✅ Done | |
| `py_runtime.h` | ~400 | **207** | ✅ Done | Excludes class/context-manager decls |
| `py_transpiler.hpp` | ~250 | **108** | ✅ Done | |
| `py_scope.cpp` | ~400 | **245** | ✅ Done | |
| `py_exception.cpp` | ~400 | **—** | ✅ (merged) | Exception handling in `py_runtime.cpp` |
| `py_print.cpp` | ~150 | **405** | ✅ Done | AST pretty-printer |
| **Total** | **~26,400** | **~10,865** | | Lower because class system and generators not yet built |

### 6.2 Runtime Function Categories (registered in `sys_func_registry.c`)

| Category | Functions | Status |
|----------|-----------|--------|
| Type conversion | `py_to_int`, `py_to_float`, `py_to_str`, `py_to_bool` | ✅ Done |
| Arithmetic | `py_add`, `py_subtract`, `py_multiply`, `py_divide`, `py_floor_divide`, `py_modulo`, `py_power`, `py_negate`, `py_positive`, `py_bit_not` | ✅ Done (`py_matmul` not needed yet ❌) |
| Comparison | `py_eq`, `py_ne`, `py_lt`, `py_le`, `py_gt`, `py_ge`, `py_is`, `py_is_not`, `py_contains` | ✅ Done |
| Logical / Bitwise | `py_bit_and`, `py_bit_or`, `py_bit_xor`, `py_lshift`, `py_rshift`, `py_is_truthy` | ✅ Done |
| Object / Attribute | `py_getattr`, `py_setattr`, `py_hasattr`, `py_new_object` | ✅ Done (`py_delattr` ❌, `py_new_instance` ❌ — need class system) |
| Collections | `py_list_new/append/get/set/length`, `py_dict_new/get/set`, `py_tuple_new/set`, `py_subscript_get/set`, `py_slice_get/set` | ✅ Done |
| Iterator | `py_get_iterator`, `py_iterator_next`, `py_range_new`, `py_stop_iteration`, `py_is_stop_iteration` | ✅ Done |
| Function / Closure | `py_new_function`, `py_new_closure`, `py_alloc_env`, `py_call_function` | ✅ Done (`py_call_method` handled via method dispatchers) |
| Class | `py_class_new`, `py_compute_mro`, `py_super`, `py_isinstance`, `py_issubclass` | ❌ Deferred to v3 |
| Exception | `py_raise`, `py_check_exception`, `py_clear_exception`, `py_new_exception`, `py_exception_get_type` | ✅ Done |
| Context manager | `py_context_enter`, `py_context_exit` | ❌ Deferred (`with` statement not yet) |
| Built-in functions | `py_print`, `py_print_ex`, `py_builtin_len/type/isinstance/range/int/float/str/bool/abs/min/max/sum/enumerate/zip/sorted/reversed/repr/hash/id/input/ord/chr/map/filter/list/dict/set/tuple/round/all/any/bin/oct/hex/divmod/pow/callable/open` (40+) | ✅ Done |
| String / list / dict methods | `py_string_method` (30+), `py_list_method` (11+), `py_dict_method` (10+) | ✅ Done |
| Module variables | `py_get_module_var`, `py_set_module_var`, `py_reset_module_vars` | ✅ Done |
| Format | `py_format_value` (f-string format specs) | ✅ Done |
| **Total registered** | ~75 functions | |

---

## 7. Optimization Strategies

Reuse the same optimization playbook as the JS transpiler:

| Code | Optimization | Python-Specific Notes |
|------|-------------|----------------------|
| — | Native arithmetic | When operands are known int/float, emit `MIR_ADD`/`MIR_DADD` directly. Python's `//` (floor div) needs `MIR_DIV` + floor, not a single instruction. |
| P4 | Dual compilation | Numeric functions get boxed + native versions. Very effective for Python's mathematical code. |
| P7 | Method devirtualization | When class is known, resolve method at compile time. Especially valuable for single-inheritance classes. |
| A5 | Constructor shape pre-alloc | Pre-scan `__init__` for `self.attr = expr` patterns to pre-allocate instance shape. |
| A6 | Pointer comparison | Interned attribute names enable pointer equality for dict/attribute lookups. |
| TCO | Tail-call optimization | Python doesn't guarantee TCO, but we can optimize self-recursive tail calls for performance. |
| — | Constant folding | Fold `2 ** 10`, `len("hello")`, etc. at compile time |
| — | Type inference | Python's lack of type declarations makes this harder than JS, but assignment patterns and arithmetic usage still provide strong evidence. |

### 7.1 Python-Specific Optimizations

| Code | Optimization | Description |
|------|-------------|-------------|
| P-L1 | Range loop fast path | `for i in range(n)` compiles to a simple counted loop — no iterator object allocation |
| P-L2 | Dict literal shape | `{"a": 1, "b": 2}` pre-allocates the Map shape at compile time |
| P-S1 | String interning | Attribute names, dict literal keys, and short string constants auto-interned |
| P-C1 | Comprehension inlining | Simple list comprehensions compiled as inline loops, not closure calls |
| P-I1 | `isinstance` fast path | Single-class check compiles to pointer comparison on `__class__` |

---

## 8. Standard Library Modules (Built-In) ❌ Deferred to v3+

Phase 1 includes a minimal set of "built-in" modules implemented in C++:

| Module | Implementation Strategy | Est. LOC | Status |
|--------|------------------------|----------|--------|
| `json` | Delegate to Lambda's `parse_json_to_item()` / `format_json()` | ~100 | ❌ Not yet |
| `math` | Delegate to Lambda's math sys_funcs + C `<math.h>` | ~150 | ❌ Not yet |
| `re` | Delegate to Lambda's RE2 wrapper | ~200 | ❌ Not yet |
| `os.path` | Delegate to Lambda's `path.c` utilities | ~100 | ❌ Not yet |
| `sys` | `sys.argv`, `sys.exit`, `sys.stdout`, `sys.stderr` | ~80 | ❌ Not yet |
| `io` | Basic `open()`, `read()`, `write()`, `close()` file I/O | ~200 | ❌ Not yet (bare `open()` builtin for read-only exists) |
| `string` | Constants (`ascii_lowercase`, etc.), `Template` (maybe) | ~80 | ❌ Not yet |
| `collections` | `OrderedDict` (alias for dict), `defaultdict`, `Counter` | ~300 | ❌ Not yet |

> **Issue [S4]:** Python's `import` system is complex (packages, `__init__.py`, relative imports, `sys.path` search). For Phase 1, we support only built-in modules and single-file imports via `from module import func`. Full package resolution is deferred.

---

## 9. Build Integration

### 9.1 Directory Structure

```
lambda/
  py/
    build_py_ast.cpp
    transpile_py_mir.cpp
    py_runtime.cpp
    py_runtime.h
    py_builtins.cpp
    py_class.cpp
    py_string.cpp
    py_collections.cpp
    py_iterator.cpp
    py_scope.cpp
    py_exception.cpp
    py_ast.hpp
    py_transpiler.hpp
    py_print.cpp
  tree-sitter-python/
    src/
      parser.c             (~230K lines, auto-generated)
      scanner.c            (~2K lines, indent/dedent handling)
    bindings/c/
      tree-sitter-python.h
    grammar.js
    libtree-sitter-python.a
```

### 9.2 Build Config Changes (`build_lambda_config.json`)

Add to `source_dirs`:
```json
"lambda/py"
```

Add to `includes`:
```json
"lambda/tree-sitter-python/bindings/c"
```

Add to `libraries`:
```json
{
    "name": "tree-sitter-python",
    "include": "lambda/tree-sitter-python/bindings/c",
    "lib": "lambda/tree-sitter-python/libtree-sitter-python.a",
    "link": "static"
}
```

### 9.3 CLI Integration (`main.cpp`)

Add a `"py"` command handler following the `"js"` pattern:

```c
if (argc >= 2 && strcmp(argv[1], "py") == 0) {
    if (argc >= 3) {
        const char* py_file = argv[2];
        char* py_source = read_text_file(py_file);
        Item result = transpile_py_to_mir(&runtime, py_source, py_file);
        // format and print result
    }
}
```

CLI usage:
```bash
./lambda.exe py script.py              # Run a Python script
./lambda.exe py script.py --help       # Python runner help
```

---

## 10. Testing Strategy

### 10.1 Test Structure

```
test/
  test_py_gtest.cpp                    # GTest harness for Python transpiler
  python/
    test_basic.py          test_basic.txt
    test_arithmetic.py     test_arithmetic.txt
    test_strings.py        test_strings.txt
    test_lists.py          test_lists.txt
    test_dicts.py          test_dicts.txt
    test_functions.py      test_functions.txt
    test_closures.py       test_closures.txt
    test_classes.py        test_classes.txt
    test_exceptions.py     test_exceptions.txt
    test_iterators.py      test_iterators.txt
    test_comprehensions.py test_comprehensions.txt
    test_scope.py          test_scope.txt
    test_builtins.py       test_builtins.txt
    test_import.py         test_import.txt
```

Each `.py` file has a corresponding `.txt` file with expected output (same pattern as Lambda `.ls` tests).

### 10.2 Phased Testing

> **Note:** Tests live in `test/py/` as `.py` + `.txt` pairs. Run with `./lambda.exe py <file.py>`.

| Phase | Features | Tests | Status |
|-------|----------|-------|--------|
| **Phase 1** | Literals, arithmetic, variables, `print()`, `if`/`elif`/`else`, `while`, `for`/`range`, functions, return | `test_py_basic`, `test_py_extended` | ✅ Done |
| **Phase 2** | Strings, lists, dicts, tuples, sets, slicing, comprehensions, f-strings, `%` formatting | `test_py_slicing`, `test_py_comprehensions`, `test_py_formatting`, `test_py_string_methods_v2`, `test_py_dict_methods_v2` | ✅ Done |
| **Phase 3** | Classes, inheritance, `super()`, `__init__`, `isinstance` | `test_py_classes` (pending) | ❌ Deferred to v3 |
| **Phase 4** | Closures, `global`/`nonlocal`, nested functions, default & keyword args, `*args` | `test_py_closures`, `test_py_defaults`, `test_py_builtins_v2` | ✅ Done |
| **Phase 5** | Exceptions, `try`/`except`/`finally`, `raise`, exception type matching | `test_py_exceptions` | ✅ Done (`with` statement deferred) |
| **Phase 6** | Iterators (`iter`/`next`), generators, `yield`, `yield from` | `test_py_extended` (iter/next ✅) | ⚠️ Partial (`iter`/`next` ✅, generators ❌ deferred to v4+) |
| **Phase 7** | Built-in modules (`json`, `math`, `re`), `import` | (pending) | ❌ Deferred to v3+ |

---

## 11. Size Estimates

### 11.1 Lines of Code

| Component | Estimated | Actual | Notes |
|-----------|-----------|--------|-------|
| Hand-written runtime (core done) | ~26,400 | **~10,865** | Lower because class system, generators, std modules not yet built |
| Auto-generated parser | ~230,000 | ~230,000 | Unchanged |
| Grammar static lib | ~700–800 KB | ~700–800 KB | Unchanged |

The actual LOC is ~59% lower than estimated because:
- Class system (~1,200 + 200 transpiler lines) deferred to v3
- Generator/yield support (~600 lines) deferred to v4+
- Standard library modules (~1,210 lines) deferred to v3+
- String/list/dict methods consolidated into `py_builtins.cpp` (no separate files)
- Exception handling consolidated into `py_runtime.cpp` (no separate `py_exception.cpp`)

### 11.2 Binary Size Impact

| Component | Size Added |
|-----------|-----------|
| Hand-written Python runtime (~26K LOC, release build) | ~250–350 KB |
| tree-sitter-python static lib | ~500–600 KB |
| **Total added to lambda.exe** | **~800 KB – 1.0 MB** |

Current release exe is ~8 MB → would become ~9 MB with Python support.

---

## 12. Open Issues & Suggestions Summary

### Issues

| ID | Issue | Severity | Status | Notes |
|----|-------|----------|--------|-------|
| G1 | External scanner required | Low | ✅ Resolved | `scanner.c` bundled with tree-sitter-python |
| S1 | Two-pass function analysis | Medium | ✅ Resolved | `pm_analyze_globals()` + `pm_collect_local_defs()` pre-pass implemented |
| S2 | Keyword argument convention | Medium | ⚠️ Partial | Positional + keyword args ✅, `*args` ✅, `**kwargs` ❌ not yet |
| S3 | C3 MRO for multiple inheritance | Medium | ❌ Deferred | Class system deferred to v3 |
| S4 | Import system complexity | High | ❌ Deferred | `from module import name` parsed but not resolved; modules deferred to v3+ |

### Suggestions

| ID | Suggestion | Status |
|----|-----------|--------|
| T1 | Cap int at int64 initially | ✅ Done — int64 cap in place |
| T2 | Tuple immutability via flag | ⚠️ Not done — tuples stored as arrays without immutability flag |
| T3 | StopIteration as ItemError | ⚠️ Changed — uses `py_stop_iteration_sentinel` (a special Map object), works correctly |

### Additional Suggestions

| ID | Suggestion | Status |
|----|-----------|--------|
| T4 | Skip type annotations at runtime | ✅ Done — annotations parsed and ignored |
| T5 | `__slots__` optimization | ❌ Deferred — needs class system |
| T6 | Shared `py_` / `js_` runtime functions | ⚠️ Not done — functions are separate; worth revisiting after class system |
| T7 | Python `print()` → Lambda `format` | ✅ Done — `py_print`/`py_print_ex` with `sep`/`end` support |

---

## Appendix A: Comparison — JS vs Python Transpiler

| Aspect | JS Transpiler | Python Transpiler |
|--------|--------------|------------------|
| Source grammar | tree-sitter-javascript | tree-sitter-python |
| Parser size | 84K lines / 391 KB lib | ~230K lines / ~750 KB lib |
| Entry function | `js_main()` | `py_main()` |
| Paradigm | Imperative (statements + expressions) | Imperative (statements + expressions) |
| Variables | `var`/`let`/`const` (explicit) | Assignment-based (implicit local) |
| Scoping | Function + block scope | LEGB: Local, Enclosing, Global, Built-in |
| Scope override | N/A | `global`, `nonlocal` keywords |
| Objects | Prototype chain (single) | Class hierarchy with C3 MRO (multiple) |
| Item type for `None`/`null` | `null` → LMD_TYPE_NULL, `undefined` → LMD_TYPE_UNDEFINED | `None` → LMD_TYPE_NULL only |
| New TypeIds needed | LMD_TYPE_UNDEFINED | None (reuse existing) |
| Iteration | `for...of` / `for...in` | `for x in iterable` with `__iter__`/`__next__` protocol |
| Comprehensions | N/A | List/dict/set/generator comprehensions |
| Closures | Shared scope env (mutable) | Cell variables (shared scope env, same model) |
| Error handling | `try`/`catch`/`finally`, global exception state | `try`/`except`/`finally`, global exception state (same model) |
| Calling convention | Positional only | Positional + keyword + `*args` + `**kwargs` |
| DOM integration | Full (Radiant bridge) | None needed |
| Estimated hand-written LOC | 23,523 | ~26,400 |

## Appendix B: Implementation Priority

Recommended implementation order to maximize incremental testability:

1. **Parser + AST builder** — Get tree-sitter-python parsing, build typed AST. Verify with AST printer. ✅ Done
2. **Basic transpiler** — Literals, arithmetic, variables, `print()`, `if`/`while`/`for`. Run first programs. ✅ Done
3. **Functions + closures** — `def`, return values, default params, closures, `global`/`nonlocal`. ✅ Done
4. **Data structures** — Lists, dicts, tuples, sets, slicing, comprehensions. ✅ Done
5. **Classes** — Single inheritance first. `__init__`, methods, `isinstance`. Then multiple inheritance + MRO. ❌ Deferred to v3
6. **Exceptions** — `try`/`except`/`finally`/`raise`, exception hierarchy, `with` statement. ✅ Done (exceptions ✅, `with` ❌ deferred)
7. **Iterators + generators** — Iterator protocol, `yield`, generator functions. ⚠️ Partial (`iter`/`next` ✅, generators ❌ deferred to v4+)
8. **Built-in modules** — `json`, `math`, `re`, `sys`, basic file I/O. ❌ Deferred to v3+
9. **Optimizations** — Native arithmetic fast paths, range loop optimization, shape pre-allocation. ✅ Partially done (native arithmetic, range loop fast path)
