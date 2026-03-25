# Python Transpiler v2: Closing the Core Language Gaps

## 1. Executive Summary

LambdaPy v1 established a working Python-to-MIR pipeline (~7.8K LOC) with solid coverage of expressions, basic control flow, functions, and 29 builtins. However, several features that appear in virtually every non-trivial Python program remain missing: **default arguments**, **keyword arguments**, **slicing**, **list comprehensions**, **lambda expressions**, **closures**, **try/except**, and **string formatting**. This proposal targets those gaps in priority order.

### Architecture Position

```
v1:  Python AST → direct MIR IR → native                (done, ~7.8K LOC)
       Core expressions, control flow, functions, 29 builtins
       17 string methods, 11 list methods, 7 dict methods
v2:  Close core language gaps                            (this proposal)
       Phase A: Default & keyword arguments               most-blocking gap
       Phase B: Slicing & string formatting               data processing essentials
       Phase C: List/dict comprehensions & lambda          idiomatic Python
       Phase D: Closures & scope enforcement               nested function correctness
       Phase E: Exception handling (try/except/raise)      error handling
       Phase F: Additional builtins & method gaps          stdlib coverage
```

### Current vs Target Coverage

| Metric | v1 (Current) | v2 (Target) |
|--------|-------------|-------------|
| Core expressions & operators | 100% | 100% |
| Function argument handling | Positional only | Default, keyword, `*args`, `**kwargs` |
| Slicing | Not supported | Full (`a[1:3]`, `a[::-1]`, slice assignment) |
| String formatting | Stub | `f"{x:.2f}"`, `str.format()`, `%` operator |
| Comprehensions | Not supported | List, dict, set (single `for` + `if`) |
| Lambda | Not supported | Full |
| Closures | Not supported | Variable capture via shared env |
| Exception handling | `assert` only | `try`/`except`/`finally`/`raise` |
| `global`/`nonlocal` enforcement | Parsed, no-op | Runtime-enforced |
| Builtins | 29 | ~40 |
| Estimated new LOC | — | ~2,500–3,000 |

### Non-Goals (v2)

- Class definitions and OOP (deferred to v3)
- `import` / module system (deferred to v3)
- `async`/`await`/generators/`yield` (deferred to v4+)
- `match`/`case` (Python 3.10+) (deferred)
- Decorator application (deferred to v3, after classes)
- Arbitrary-precision integers

---

## 2. Phase A: Default & Keyword Arguments

**Goal:** Enable `def f(x, y=10):` and `f(y=20, x=1)`. This is the single most impactful gap — nearly every Python library function uses default parameters.

**Estimated effort:** ~350 LOC across transpiler and runtime.

### A1. Default Parameter Values

**Current state:** `build_py_ast.cpp` parses `PY_AST_NODE_DEFAULT_PARAMETER` nodes (name + default expression). The transpiler recognizes the node type during parameter counting but does not emit the default value.

**Design:** At function entry, emit a check for each default parameter: if the caller provided fewer arguments than the full parameter count, fill missing parameters from the right with their default values.

```
Python:  def greet(name, greeting="Hello"):
             return f"{greeting}, {name}!"

MIR:     pyf_greet: func i64, i64:_py_name, i64:_py_greeting
             // _py_greeting may be ITEM_NULL if caller omitted it
             cmp = EQ(_py_greeting, PY_ITEM_NULL_VAL)
             BF L_no_default, cmp
             MOV _py_greeting, <boxed "Hello">   // default value
             L_no_default:
             ...body...
         endfunc
```

**Implementation steps:**

1. In `pm_compile_function()`, after creating MIR params, walk the parameter list for `DEFAULT_PARAMETER` nodes
2. For each default param: emit `cmp reg, PY_ITEM_NULL_VAL` → branch → emit default expression → label
3. At call sites: when `argc < param_count`, pad remaining args with `PY_ITEM_NULL_VAL`

**Files:** `transpile_py_mir.cpp` (function compilation + call emission)
**Lines:** ~80

### A2. Keyword Arguments in Calls

**Current state:** `PY_AST_NODE_KEYWORD_ARGUMENT` nodes are parsed with name+value. The transpiler skips them with `continue`.

**Design:** Resolve keyword arguments at compile time by matching argument names to parameter names. Emit arguments in the correct positional order.

```
Python:  greet(greeting="Hi", name="World")

→ compile-time reorder:
         arg[0] = "World"     // matches param 'name' at position 0
         arg[1] = "Hi"        // matches param 'greeting' at position 1
         CALL pyf_greet(arg[0], arg[1])
```

**Implementation steps:**

1. In `pm_transpile_call()`, when calling a known user function:
   - Collect all keyword arguments into a name→expression array
   - Look up the function's parameter list from `func_entries[]`
   - Build a positional argument array: for each param position, use the keyword arg if present, else the positional arg, else `PY_ITEM_NULL_VAL` (triggers default)
2. For builtin calls: add keyword support to `print()` (`sep`, `end`) and `sorted()` (`key`, `reverse`)

**Files:** `transpile_py_mir.cpp` (~120 lines), `py_builtins.cpp` (~50 lines for print kwargs)
**Lines:** ~170

### A3. `*args` Variadic Parameters

**Design:** When a function is declared with `*args`, collect excess positional arguments into a list.

```
Python:  def f(a, *args):
             print(args)

MIR:     pyf_f: func i64, i64:_py_a, i64:_py__args_raw_1, ..., i64:_py__args_raw_N
         // pack excess args into a list
         _py_args = CALL py_list_new(...)
         // for each excess param: CALL py_list_append(_py_args, _py__args_raw_i)
```

**Alternative (simpler):** Pass all arguments via a stack array + argc, like builtins do. The function unpacks named params from the array, then wraps the remainder in a list for `*args`.

**Files:** `transpile_py_mir.cpp`
**Lines:** ~100

### Test Plan

```python
# test_py_defaults.py
def greet(name, greeting="Hello"):
    return greeting + ", " + name

print(greet("World"))              # Hello, World
print(greet("World", "Hi"))        # Hi, World
print(greet(greeting="Hey", name="Lambda"))  # Hey, Lambda

def make_list(x, y=10, z=20):
    return [x, y, z]

print(make_list(1))                # [1, 10, 20]
print(make_list(1, 2))             # [1, 2, 20]
print(make_list(1, z=30))          # [1, 10, 30]
```

---

## 3. Phase B: Slicing & String Formatting

**Goal:** Enable `a[1:3]`, `s[::-1]`, `f"{x:.2f}"`, and `"hello %s" % name`. These are used pervasively in data-processing Python code.

**Estimated effort:** ~450 LOC.

### B1. Slice Operations

**Current state:** `PY_AST_NODE_SLICE` is parsed with start/stop/step. `py_subscript_get` handles single-index access but returns `ItemNull` for slices.

**Design:** Add a `py_slice_get(obj, start, stop, step)` runtime function. The transpiler detects slice nodes in subscript positions and emits the 3-arg call instead of the 1-arg subscript.

**Runtime semantics (matching Python):**
- `None` start → 0 (or len-1 if step < 0)
- `None` stop → len (or -len-1 if step < 0)
- `None` step → 1
- Negative indices wrapped: `idx = idx < 0 ? idx + len : idx`
- Clamped to `[0, len]`

**Implementation:**

1. **Runtime** (`py_runtime.cpp`):
   - `py_slice_get(Item obj, Item start, Item stop, Item step)` → returns new list/string
   - `py_slice_set(Item obj, Item start, Item stop, Item step, Item value)` → mutates list in place
   - Handle string slicing (returns new string), list slicing (returns new list)

2. **Transpiler** (`transpile_py_mir.cpp`):
   - In `pm_transpile_subscript()`, check if `subscript->index` is `PY_AST_NODE_SLICE`
   - If slice: emit start/stop/step expressions (or `PY_ITEM_NULL_VAL` for omitted), call `py_slice_get`
   - In `pm_transpile_assignment()` for subscript targets with slice: call `py_slice_set`

**Files:** `py_runtime.cpp` (~120 lines), `py_runtime.h` (2 declarations), `transpile_py_mir.cpp` (~40 lines), `sys_func_registry.c` (2 entries)
**Lines:** ~165

### B2. String Formatting — `%` Operator

**Current state:** `py_modulo` detects `LMD_TYPE_STRING` on the left and returns the string unchanged.

**Design:** Implement basic `%` formatting for the common specifiers: `%s`, `%d`, `%f`, `%r`, `%x`, `%%`.

```
Python:  "Name: %s, Age: %d" % ("Alice", 30)
```

**Implementation:**
- In `py_modulo()`, when left is string: scan format string for `%` specifiers
- Right operand is either a single value or a tuple of values
- Build result string via `StrBuf`, intern via `heap_create_name()`

**Files:** `py_runtime.cpp` (~80 lines)
**Lines:** ~80

### B3. String Formatting — `str.format()`

**Current state:** `py_string_method` matches `"format"` but returns the string unchanged.

**Design:** Implement positional `{}` and indexed `{0}` replacement in the format string.

```
Python:  "Name: {}, Age: {}".format("Alice", 30)
         "Name: {0}, Age: {1}".format("Alice", 30)
```

**Implementation:**
- In `py_str_format()`, scan for `{...}` patterns
- `{}` → use next positional argument
- `{N}` → use argument at index N
- `{name}` → keyword lookup (skip for now)
- Format specs (`{:.2f}`) → call `py_format_value(item, spec)` helper

**Files:** `py_builtins.cpp` (~80 lines)
**Lines:** ~80

### B4. F-string Format Specs

**Current state:** F-strings emit basic interpolation (`f"{x}"` calls `py_to_str` on x). Format specs like `f"{x:.2f}"` are not handled.

**Design:** Extend the f-string transpiler to recognize format specs in the AST and call a `py_format_spec(value, spec_string)` runtime function.

**Files:** `transpile_py_mir.cpp` (~30 lines), `py_runtime.cpp` (~60 lines)
**Lines:** ~90

### Test Plan

```python
# test_py_slicing.py
a = [0, 1, 2, 3, 4, 5]
print(a[1:4])           # [1, 2, 3]
print(a[:3])            # [0, 1, 2]
print(a[3:])            # [3, 4, 5]
print(a[::2])           # [0, 2, 4]
print(a[::-1])          # [5, 4, 3, 2, 1, 0]

s = "Hello, World!"
print(s[7:12])          # World
print(s[::-1])          # !dlroW ,olleH

# test_py_formatting.py
print("Hello, %s!" % "World")           # Hello, World!
print("Pi is %.2f" % 3.14159)           # Pi is 3.14
print("{} + {} = {}".format(1, 2, 3))   # 1 + 2 = 3
print(f"Value: {42:05d}")               # Value: 00042
```

---

## 4. Phase C: List/Dict Comprehensions & Lambda

**Goal:** Enable `[x*2 for x in lst]`, `{k: v for k, v in items}`, and `lambda x: x+1`. These are the most idiomatic Python constructs and appear in virtually every Python tutorial.

**Estimated effort:** ~400 LOC.

### C1. List Comprehensions

**Current state:** `PY_AST_NODE_LIST_COMPREHENSION` is fully built with element expression, target variable, iterable, and optional condition list. The transpiler has no case for it.

**Design:** Emit as an inline loop that builds a list:

```
Python:  squares = [x*2 for x in range(5) if x > 1]

MIR:     result = CALL py_list_new(0)
         iter = emit(range(5))
         length = CALL py_builtin_len(iter)
         idx = BOX_INT(0)
         L_loop:
           BF L_end, (idx < length)
           _py_x = CALL py_subscript_get(iter, idx)
           // if condition
           cmp = CALL py_gt(_py_x, BOX_INT(1))
           BF L_continue, cmp
           elem = emit(x*2)
           CALL py_list_append(result, elem)
           L_continue:
           idx = CALL py_add(idx, BOX_INT(1))
           JMP L_loop
         L_end:
         // result register holds the comprehension output
```

**Implementation:**
- Add `case PY_AST_NODE_LIST_COMPREHENSION:` in `pm_transpile_expression()`
- Push a new scope for the comprehension variable
- Emit the iteration loop with optional `if` filtering
- Pop scope

**Scope:** Comprehension variables are local to the comprehension (Python 3 semantics). Push/pop a scope around the loop.

**Files:** `transpile_py_mir.cpp` (~100 lines)
**Lines:** ~100

### C2. Dict Comprehensions

Same pattern as list comprehensions, but emit `py_dict_new()` + `py_dict_set(result, key_expr, val_expr)`.

**Files:** `transpile_py_mir.cpp` (~60 lines)
**Lines:** ~60

### C3. Set Comprehensions

Same pattern as list comprehensions using `py_list_new()` + `py_list_append()` (sets are stored as lists in v1). Future v3 could add a proper set type.

**Files:** `transpile_py_mir.cpp` (~40 lines)
**Lines:** ~40

### C4. Lambda Expressions

**Current state:** `PY_AST_NODE_LAMBDA` is parsed with params and body expression. Not transpiled.

**Design:** Compile lambda as an anonymous function, similar to `def` but with an auto-generated name and a single-expression body that is implicitly returned.

```
Python:  f = lambda x, y: x + y

MIR:     pyf__lambda_0: func i64, i64:_py_x, i64:_py_y
             result = CALL py_add(_py_x, _py_y)
             RET result
         endfunc

         // in py_main: box function pointer into Item
         _py_f = CALL py_new_function(&pyf__lambda_0, 2)
```

**Implementation:**
1. During function collection (Phase 1), also collect lambda expressions — assign each a name like `_lambda_0`
2. Compile each lambda as a MIR function with `pyf__lambda_N` name
3. In `pm_transpile_expression()` for `PY_AST_NODE_LAMBDA`: emit `py_new_function(ptr, param_count)`

**Files:** `transpile_py_mir.cpp` (~80 lines)
**Lines:** ~80

### Test Plan

```python
# test_py_comprehensions.py
squares = [x**2 for x in range(6)]
print(squares)                              # [0, 1, 4, 9, 16, 25]

evens = [x for x in range(10) if x % 2 == 0]
print(evens)                                # [0, 2, 4, 6, 8]

words = ["hello", "world", "python"]
lengths = {w: len(w) for w in words}
print(lengths)                              # {hello: 5, world: 5, python: 6}

# test_py_lambda.py
double = lambda x: x * 2
print(double(5))                            # 10

nums = [3, 1, 4, 1, 5]
print(sorted(nums, key=lambda x: -x))      # [5, 4, 3, 1, 1]  (requires Phase A kwargs first)

add = lambda a, b: a + b
print(add(10, 20))                          # 30
```

---

## 5. Phase D: Closures & Scope Enforcement

**Goal:** Enable nested functions that capture variables from enclosing scopes, and enforce `global`/`nonlocal` declarations at runtime.

**Estimated effort:** ~500 LOC.

### D1. Closure Variable Capture

**Current state:** `PyMirTranspiler` has `scope_env_reg`, `scope_env_slot_count`, and `PyFuncCollected.captures[]` fields — infrastructure was laid out in v1 but not wired up. `py_new_closure()` and `py_alloc_env()` are registered in `sys_func_registry.c`.

**Design (same as LambdaJS closures):** Captured variables are stored in a heap-allocated environment array (`uint64_t* env`). Inner functions receive the env pointer as a hidden first parameter.

```
Python:  def make_adder(n):
             def add(x):
                 return x + n    # captures 'n'
             return add

MIR:     pyf_make_adder: func i64, i64:_py_n
             env = CALL py_alloc_env(1)            // 1 captured var
             MOV [env+0], _py_n                     // store n in env slot 0
             result = CALL py_new_closure(&pyf_add, 1, env, 1)
             RET result
         endfunc

         pyf_add: func i64, i64:_py__env, i64:_py_x
             _py_n = MOV [_py__env+0]               // load n from env slot 0
             result = CALL py_add(_py_x, _py_n)
             RET result
         endfunc
```

**Implementation steps:**

1. **Capture analysis** (new pre-pass after function collection):
   - For each inner function, walk its body and collect referenced identifiers
   - If an identifier is not a local or parameter but exists in an enclosing function's scope → mark as captured
   - Record captures in `PyFuncCollected.captures[]` with env slot indices

2. **Env allocation at closure creation point:**
   - At the point where the inner function is referenced (not where it's compiled), emit `py_alloc_env(N)` and store captured values into env slots

3. **Env access in inner function:**
   - Inject a hidden `_py__env` parameter as the first argument
   - For captured variables, load from env pointer instead of a local register

4. **Mutable captures:**
   - When a captured variable is modified in either scope, write back to the env slot
   - Both the outer and inner function access the shared env slot (same pointer)

**Files:** `transpile_py_mir.cpp` (~300 lines), `py_runtime.cpp` (existing functions)
**Lines:** ~300

### D2. `global` / `nonlocal` Enforcement

**Current state:** `global x` and `nonlocal x` are parsed and handled in scope analysis but are no-ops at runtime (the transpiler treats them as regular variables).

**Design:** Module-level variables use the existing `py_module_vars[]` indexed array. `global x` inside a function means reads/writes to `x` go through `py_get_module_var(idx)` / `py_set_module_var(idx, val)` instead of a local register.

**Implementation:**
1. During scope analysis, when `global x` is encountered: mark `x` with `PY_VAR_GLOBAL`
2. In the transpiler, when accessing a `PY_VAR_GLOBAL` variable: emit `py_get_module_var(idx)` / `py_set_module_var(idx, val)`
3. Similarly for `nonlocal`: route to the enclosing function's env slot (requires closure infrastructure from D1)

**Files:** `transpile_py_mir.cpp` (~80 lines), `py_scope.cpp` (~20 lines)
**Lines:** ~100

### Test Plan

```python
# test_py_closures.py
def make_counter():
    count = 0
    def increment():
        nonlocal count
        count = count + 1
        return count
    return increment

c = make_counter()
print(c())    # 1
print(c())    # 2
print(c())    # 3

def make_adder(n):
    return lambda x: x + n

add5 = make_adder(5)
print(add5(10))    # 15
print(add5(20))    # 25

# test_py_global.py
total = 0

def add_to_total(x):
    global total
    total = total + x

add_to_total(10)
add_to_total(20)
print(total)       # 30
```

---

## 6. Phase E: Exception Handling

**Goal:** Enable `try`/`except`/`finally`/`raise`. The runtime already has `py_raise()`, `py_check_exception()`, and `py_clear_exception()` — the transpiler just needs to emit the control flow.

**Estimated effort:** ~400 LOC.

### E1. Design

Python exceptions use global thread state (already implemented in `py_runtime.cpp`):

```c
static bool py_exception_pending;
static Item py_exception_value;
```

**Mapping to MIR control flow:**

```
Python:                          MIR:
try:                             // emit body
    body                         // after each statement that could raise:
except ValueError as e:            check = CALL py_check_exception()
    handler                        BT L_except_0, check
finally:                         JMP L_finally         // body completed OK
    cleanup                      L_except_0:
                                   exc = CALL py_clear_exception()
                                   // match exception type (optional)
                                   MOV _py_e, exc
                                   emit(handler)
                                   JMP L_finally
                                 L_finally:
                                   emit(cleanup)
```

**Key decisions:**

- **Exception check granularity:** Check after every call to a runtime function that might raise (arithmetic, subscript, builtin calls). This is the same approach as LambdaJS.
- **Type matching:** `except ValueError` checks the exception's type name string. `except Exception` catches all. Bare `except:` catches all.
- **`finally` semantics:** Always runs — after normal completion, after exception handling, or after exception propagation. Use a flag register to track whether to re-raise.
- **Re-raise:** `raise` with no argument → `py_raise(saved_exception)`

### E2. `raise` Statement

Map `raise expr` to:
1. Evaluate `expr`
2. Call `py_raise(expr)`
3. Jump to nearest except handler (or function exit)

If `expr` is a call like `ValueError("msg")`, and we don't have classes yet: create a simple map `{type: "ValueError", message: "msg"}` via `py_new_exception()`.

### Test Plan

```python
# test_py_exceptions.py
def divide(a, b):
    if b == 0:
        raise "division by zero"
    return a / b

try:
    result = divide(10, 0)
except:
    print("caught error")       # caught error

try:
    x = [1, 2, 3]
    print(x[10])
except:
    print("index error")        # index error

def safe_divide(a, b):
    try:
        return a / b
    except:
        return 0
    finally:
        print("done")

print(safe_divide(10, 2))       # done \n 5.0
print(safe_divide(10, 0))       # done \n 0
```

---

## 7. Phase F: Additional Builtins & Method Gaps

**Goal:** Fill in the most commonly used builtins and methods that are still missing.

**Estimated effort:** ~400 LOC.

### F1. New Builtins

| Function | Implementation | LOC |
|----------|---------------|-----|
| `round(x, n=0)` | `round()` / `pow(10,n)` scaling | ~20 |
| `all(iterable)` | Loop, short-circuit on falsy | ~15 |
| `any(iterable)` | Loop, short-circuit on truthy | ~15 |
| `bin(n)` / `oct(n)` / `hex(n)` | Integer to string conversion | ~30 |
| `divmod(a, b)` | Return `(a // b, a % b)` tuple | ~15 |
| `pow(base, exp, mod=None)` | 3-arg pow with modular arithmetic | ~20 |
| `callable(x)` | Check if `get_type_id(x) == LMD_TYPE_FUNC` | ~5 |
| `iter(x)` / `next(it)` | Delegate to existing iterator protocol | ~20 |
| `open(path)` | Read file contents via `read_text_file()` | ~25 |

**Files:** `py_builtins.cpp` (~165 lines), `py_runtime.h` (declarations), `sys_func_registry.c` (entries), `transpile_py_mir.cpp` (direct-call cases)

### F2. Additional String Methods

| Method | Implementation | LOC |
|--------|---------------|-----|
| `index(sub)` | Like `find` but raises on miss | ~10 |
| `rfind(sub)` | Reverse search | ~15 |
| `splitlines()` | Split on `\n` | ~15 |
| `isalnum()` | Check alpha or digit | ~5 |
| `isspace()` | Check whitespace | ~5 |
| `islower()` / `isupper()` | Case check | ~10 |
| `swapcase()` | Invert case | ~10 |
| `center(width, fill)` | Pad both sides | ~15 |
| `ljust(width)` / `rjust(width)` | Pad left/right | ~15 |
| `zfill(width)` | Zero-pad | ~10 |

**Files:** `py_builtins.cpp` (~110 lines)

### F3. Additional Dict Methods

| Method | Implementation | LOC |
|--------|---------------|-----|
| `copy()` | Shallow copy via shape walk | ~15 |
| `setdefault(key, default)` | Get or insert | ~15 |
| `popitem()` | Remove last item | ~15 |

**Files:** `py_builtins.cpp` (~45 lines)

### F4. `sorted()` and `list.sort()` with `key=` and `reverse=`

Add optional `key` function and `reverse` boolean to the sort implementation. The sort calls `py_call_function(key_fn, &item, 1)` for each element to produce sort keys, then sorts by keys.

**Files:** `py_builtins.cpp` (~60 lines)

### F5. `print()` with `sep=` and `end=`

Currently `py_print` joins with `" "` and ends with `"\n"`. Check for keyword args `sep` and `end` in the transpiler and pass them to a new `py_print_ex(args, argc, sep, end)` function.

**Files:** `py_builtins.cpp` (~30 lines), `transpile_py_mir.cpp` (~20 lines)

### Test Plan

```python
# test_py_builtins_v2.py
print(round(3.14159, 2))        # 3.14
print(all([True, True, False]))  # False
print(any([False, False, True])) # True
print(bin(42))                   # 0b101010
print(hex(255))                  # 0xff
print(divmod(17, 5))             # (3, 2)
print(callable(print))           # True

# test_py_string_methods_v2.py
print("hello world".index("world"))   # 6
print("hello\nworld".splitlines())    # [hello, world]
print("hello".center(11, "-"))        # ---hello---
print("42".zfill(5))                  # 00042

# test_py_sort_key.py
words = ["banana", "pie", "Washington", "a"]
print(sorted(words, key=len))           # [a, pie, banana, Washington]
print(sorted([3, 1, 2], reverse=True))  # [3, 2, 1]

# test_py_print_kwargs.py
print(1, 2, 3, sep="-")         # 1-2-3
print("hello", end="!\n")       # hello!
```

---

## 8. Implementation Order & Dependencies

```
Phase A ─────────────────────────────────── (no deps, start here)
  │
  ├─▶ Phase B ────────────────────────────── (no deps on A, can parallelize)
  │
  ├─▶ Phase C ────────────────────────────── (lambda benefits from A for defaults)
  │     │
  │     └─▶ Phase D ──────────────────────── (closures need lambda from C)
  │           │
  │           └─▶ Phase E ────────────────── (exceptions benefit from D for scope)
  │
  └─▶ Phase F ────────────────────────────── (independent, can parallelize)
```

**Recommended execution order:** A → B → C → D → E → F

Phases A, B, and F are independent and could be worked on in parallel. Phase C (lambda) benefits from A (default args). Phase D (closures) requires C (lambda). Phase E (exceptions) benefits from D (scope enforcement) but can be done independently.

---

## 9. LOC Estimate Summary

| Phase | Feature | New LOC | Files Modified |
|-------|---------|---------|----------------|
| A | Default & keyword args, `*args` | ~350 | transpile_py_mir.cpp, py_builtins.cpp |
| B | Slicing, `%` formatting, `str.format()`, f-string specs | ~450 | py_runtime.cpp, py_builtins.cpp, transpile_py_mir.cpp |
| C | List/dict/set comprehensions, lambda | ~280 | transpile_py_mir.cpp |
| D | Closures, `global`/`nonlocal` enforcement | ~400 | transpile_py_mir.cpp, py_scope.cpp |
| E | `try`/`except`/`finally`/`raise` | ~400 | transpile_py_mir.cpp |
| F | Builtins, string/dict methods, sort key, print kwargs | ~400 | py_builtins.cpp, transpile_py_mir.cpp |
| **Total** | | **~2,280** | |

Post-v2 total: ~7,800 (v1) + ~2,280 (v2) ≈ **~10,000 LOC**.

---

## 10. Testing Strategy

Each phase has dedicated test scripts (shown above). In addition:

1. **Regression:** All existing tests (`test_py_basic.py`, `test_py_extended.py`) must continue to pass after each phase.
2. **Baseline:** Lambda baseline tests (`make test-lambda-baseline`) must remain at their current pass rate.
3. **Integration test:** After all phases, run a realistic Python program that exercises multiple features together:

```python
# test_py_integration_v2.py
def fibonacci(n):
    if n <= 1:
        return n
    a, b = 0, 1
    for _ in range(n - 1):
        a, b = b, a + b
    return b

# Uses: comprehensions, lambda, default args, slicing, formatting, closures
fibs = [fibonacci(i) for i in range(10)]
print(fibs)                                    # [0, 1, 1, 2, 3, 5, 8, 13, 21, 34]
print(fibs[2:7])                               # [1, 2, 3, 5, 8]

even_fibs = list(filter(lambda x: x % 2 == 0, fibs))
print(even_fibs)                               # [0, 2, 8, 34]

def format_entry(name, value, width=10):
    return f"{name:>{width}}: {value}"

for i, f in enumerate(fibs[:5]):
    print(format_entry(f"fib({i})", f))

def make_multiplier(n):
    return lambda x: x * n

double = make_multiplier(2)
triple = make_multiplier(3)
print([double(x) for x in range(5)])           # [0, 2, 4, 6, 8]
print([triple(x) for x in range(5)])           # [0, 3, 6, 9, 12]

try:
    result = fibs[100]
except:
    print("index out of range")                # index out of range

print("all tests passed")
```

---

## Appendix: What's Deferred to v3+

| Feature | Reason for Deferral | Proposed Version |
|---------|-------------------|-----------------|
| Classes & OOP | Large surface area (~1,500+ LOC); needs prototype chain or vtable design | v3 |
| `import` / modules | Requires module resolution, namespace isolation, circular dependency handling | v3 |
| Decorators | Meaningful only with classes; function decorators are syntactic sugar for `f = decorator(f)` | v3 |
| `with` statement | Requires class protocol (`__enter__`/`__exit__`) | v3 |
| `async`/`await`/`yield` | Requires coroutine/generator runtime; fundamentally changes execution model | v4 |
| `match`/`case` | Python 3.10+ pattern matching; complex AST, low priority | v4 |
| Arbitrary-precision `int` | Most programs stay in 64-bit range; add `BigInt` type if demanded | v4 |
| `bytes`/`bytearray` | Binary data type; niche use case for a scripting transpiler | v4 |
| Nested comprehensions | `[[i*j for j in range(3)] for i in range(3)]` — needs scope stack in comprehension emit | v2.1 |
| Generator expressions (lazy) | Would require coroutine-like suspend/resume; v2 materializes eagerly as list | v4 |
