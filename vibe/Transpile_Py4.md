# Python Transpiler v4: Generators, Pattern Matching, Standard Library, async/await, Packages, and Advanced OOP

## 1. Executive Summary

LambdaPy v3 closed the major OOP and module-system gaps (~14.4K LOC). The features explicitly deferred from v3 are: **generators/`yield`**, **`match`/`case` pattern matching**, **standard library module stubs**, **`async`/`await`**, **full package imports**, and **metaclasses/advanced OOP** (including `@prop.setter`, full descriptor protocol, `__init_subclass__`, `__class_getitem__`). This proposal targets those in priority order.

> **Implementation Status:** All phases pending.

### Architecture Position

```
v1:  Python AST → MIR IR → native                          (done, ~7.8K LOC)
       Core expressions, control flow, functions, 29 builtins
v2:  Close core language gaps                               (✅ COMPLETE, ~10.9K LOC)
       Default/keyword args, slicing, f-string specs, comprehensions,
       closures, exceptions, *args, iter/next, open(), 40+ builtins
v3:  OOP and module system                                  (✅ COMPLETE, ~14.4K LOC)
       Classes, inheritance, super, dunders, with, decorators, **kwargs,
       single-file imports
v4:  Generators, pattern matching, stdlib, async, packages  (this doc, target ~20K LOC)
       Phase A: Generators and yield                        ⏳ pending
       Phase B: match/case pattern matching                 ⏳ pending
       Phase C: Standard library module stubs               ⏳ pending
       Phase D: async/await and event loop                  ⏳ pending
       Phase E: Full package system                         ⏳ pending
       Phase F: Advanced OOP (metaclasses, descriptors)     ⏳ pending
```

### Current vs Target Coverage

| Metric | v3 (actual) | v4 target |
|--------|-------------|-----------|
| `yield` / generator functions | AST stripped: `yield` silently evaluates its expression | ✅ Full generator objects with `next()`/`throw()`/`close()` |
| `yield from` | ❌ | ✅ Delegation to sub-generator |
| Generator expressions `(x for x in ...)` | Compiled as lists (eager) | ✅ Lazy generator objects |
| `match`/`case` pattern matching | ❌ | ✅ Literal, capture, sequence, mapping, class, OR patterns, guards |
| `import math` / `import os` / `import sys` | No-op warning | ✅ Built-in module stubs |
| `import json` | No-op warning | ✅ Delegates to Lambda's JSON parser |
| `import re` | No-op warning | ✅ Thin wrapper over Lambda's re2 backend |
| `async def` / `await` | `await` stripped to inner expression | ✅ Coroutines with simple event loop |
| `async for` / `async with` | ❌ | ✅ Async iterator + async context manager |
| `asyncio.run()` / `asyncio.gather()` | ❌ | ✅ Single-threaded event loop |
| `import pkg.submod` | ❌ Deferred | ✅ Package resolution with `__init__.py` |
| Relative imports (`from . import x`) | ❌ Deferred | ✅ |
| Circular imports | ❌ Not handled | ✅ Partial loading / forward references |
| `@prop.setter` / `@prop.deleter` | ❌ Deferred from v3 | ✅ Full property descriptor |
| Full descriptor protocol (`__get__`, `__set__`, `__delete__`) | ❌ | ✅ |
| `__init_subclass__` | ❌ | ✅ |
| `__class_getitem__` | ❌ | ✅ Generic type subscript (`List[int]`) |
| Metaclasses (`metaclass=Meta`) | ❌ | ✅ Basic metaclass support |
| Arbitrary-precision integers | 64-bit only | ⏳ Deferred to v5 (requires GMP integration) |
| C extension imports (`import numpy`) | ❌ | ❌ Deferred to v5 |

### Non-Goals (v4)

- Arbitrary-precision integers (requires GMP/MPIR integration — deferred to v5)
- C extension imports (`import numpy`, `import torch`)
- `threading` / `multiprocessing` (true parallelism)
- Full `asyncio` stdlib (only core primitives: `run`, `gather`, `sleep`, `create_task`)
- `ctypes` / `cffi`
- `__slots__` (deferred to v5)
- `dataclasses` module (deferred to v5, unless trivially implementable in v4 stdlib)

---

## 2. Phase A: Generators and `yield`

**Goal:** Enable generator functions (`def f(): yield x`), generator expressions `(x for x in ...)`, `yield from`, and `send()`/`throw()`/`close()` on generator objects.

**Estimated effort:** ~900 LOC. New file: `lambda/py/py_generator.cpp`, `lambda/py/py_generator.h`. Additions to `transpile_py_mir.cpp`, `py_runtime.cpp`, `py_ast.hpp`, `sys_func_registry.c`.

### A1. Core Challenge: Stackless Generator State Machine

MIR JIT compiles to native machine code — there is no native coroutine suspension. True stack-switching (like green threads or `ucontext`) would require platform-specific assembly and complicates GC root scanning.

**Solution: compile-time state machine transformation.**

A generator function is rewritten into a `switch`-based state machine at codegen time. Each `yield` point becomes a numbered case. All local variables that must survive across `yield` points are lifted into a heap-allocated generator frame (`PyGenFrame`).

```c
// lambda/py/py_generator.h
typedef struct PyGenFrame {
    int state;          // current execution state (0 = not started, -1 = exhausted)
    Item sent_value;    // value passed via send()
    Item thrown_exc;    // exception injected via throw()
    Item locals[];      // captured local variables (flexible array)
} PyGenFrame;

typedef struct PyGenObject {
    // Map fields for Lambda Item compatibility
    TypeId type_id;
    // ...
    MIR_item_t resume_func;   // the compiled state-machine MIR function
    PyGenFrame* frame;
    bool started;
    bool closed;
} PyGenObject;

Item py_gen_new(MIR_item_t resume_func, int local_count);
Item py_gen_next(Item gen);
Item py_gen_send(Item gen, Item value);
Item py_gen_throw(Item gen, Item exc_type, Item exc_val);
void py_gen_close(Item gen);
bool py_is_generator(Item x);
```

The `resume_func` has signature:
```c
// MIR prototype:
// i64 py_gen_resume(i64: frame_ptr, i64: sent_value) → Item (yielded value or StopIteration)
```

### A2. AST Changes

Add new AST node types to `py_ast.hpp`:

```c
// not yet in PY_AST_NODE_* enum — add:
PY_AST_NODE_YIELD,          // yield expr / yield
PY_AST_NODE_YIELD_FROM,     // yield from expr
```

`build_py_ast.cpp` currently strips `yield` to its inner expression. Replace with proper `PyYieldNode` construction:

```c
// py_ast.hpp additions:
typedef struct PyYieldNode {
    PyAstNode base;
    PyAstNode* value;       // NULL for bare yield
    bool is_from;           // true for yield from
} PyYieldNode;
```

`build_py_ast.cpp` fix (~30 LOC): replace the two `yield`/`await` strip cases with `PyYieldNode` construction.

### A3. Generator Detection in Transpiler — Phase 1 Extension

`pm_collect_functions_r` must detect whether a function contains `yield` anywhere in its body (recursive walk). Set a new flag:

```c
// PyFuncCollected additions:
bool is_generator;      // true if body contains yield or yield from
int yield_point_count;  // number of yield points (for state numbering)
```

**Detection walk** (~60 LOC): `pm_has_yield(PyAstNode* node)` — recursive search through the AST for `PY_AST_NODE_YIELD` or `PY_AST_NODE_YIELD_FROM`.

### A4. Generator Codegen

When `fc->is_generator` is true, `pm_compile_function` emits a different MIR function structure:

**Phase 1.9** (pre-compilation): instead of a direct function, emit:
1. A `PyGenFrame` allocation size constant.
2. A `py_gen_resume_ClassName__funcname` MIR function (the state machine).

**State machine structure** (conceptual pseudo-MIR):

```
pyf_gen_foo(i64: _frame, i64: _sent):
    _state = load _frame->state
    switch _state:
        case 0: JMP L_start
        case 1: JMP L_resume_1
        case 2: JMP L_resume_2
        ...
    L_start:
        <compile body up to first yield>
    L_yield_1:
        store _frame->state = 1
        store _frame->local_x = _py_x     // save live locals
        RET <yielded value>
    L_resume_1:
        _py_x = load _frame->local_x      // restore live locals
        _py_sent = _sent                   // sent_value available as variable
        <compile body from first yield to next yield>
    ...
    L_exhausted:
        store _frame->state = -1
        RET py_stop_iteration()
```

**Live variable analysis** (~100 LOC): `pm_gen_live_vars(PyFuncCollected* fc)` — identifies which local variables are live across at least one yield point. These are allocated as frame slots; the rest remain as MIR registers.

**Definition call site:** When a generator function is called at a call site, instead of calling the function directly, the transpiler emits:

```
reg_gen = CALL py_gen_new(pyf_gen_foo, <frame_local_count>)
// result is a generator object, not the function's return value
```

Python generator-call semantics: calling a generator function does not execute the body — it returns a generator object. Execution starts on the first `next()` call.

### A5. `yield from` Desugaring

`yield from expr` is equivalent to delegating all `next()`/`send()`/`throw()` calls to a sub-generator until it is exhausted, then resuming with the sub-generator's return value.

Compiled via a helper:

```c
Item py_yield_from(PyGenObject* outer_frame, Item subiter);
```

`py_yield_from` loops internally: each outer `next()` call calls `py_gen_send` on the sub-iterator. When the sub-iterator raises `StopIteration`, the return value is extracted and `yield from` evaluates to that value.

At the MIR level, `yield from` becomes a state machine state that returns a sentinel saying "delegate". `py_gen_next` dispatches accordingly.

### A6. Generator Expressions

Generator expressions like `(x * 2 for x in range(10) if x % 2 == 0)` are currently compiled as eager lists. In v4, they compile to anonymous generator functions:

```python
(x * 2 for x in range(10) if x % 2 == 0)
# desugars to:
def _genexpr_1(iter_arg):
    for x in iter_arg:
        if x % 2 == 0:
            yield x * 2
_genexpr_1(range(10))
```

The transpiler handles `PY_AST_NODE_GENERATOR_EXPRESSION` by synthesizing an anonymous `PyFunctionDefNode` with a `yield` body, adding it to `lambda_entries` (or `func_entries`), and replacing the generator expression with a call to that anonymous function.

**`build_py_ast.cpp` change**: currently `PY_AST_NODE_GENERATOR_EXPRESSION` is parsed but not compiled. Route it through the same path as list comprehensions but with a generator synthesis step.

**Files:** `py_generator.cpp` (~450 LOC), `py_generator.h` (~80 LOC), `py_runtime.cpp` additions (~150 LOC), `transpile_py_mir.cpp` additions (~350 LOC), `build_py_ast.cpp` fix (~50 LOC), `sys_func_registry.c` (5 entries).

### A7. `next()` / `send()` / `throw()` / `close()` Builtins

- `next(gen)` → `py_gen_next(gen)` (already planned in `py_runtime.h` via iterator protocol)
- `next(gen, default)` → call `py_gen_next`; catch `StopIteration`; return default
- `gen.send(value)` → method dispatch through `py_dict_method` or dedicated `py_gen_send`
- `gen.throw(type, val)` → `py_gen_throw`
- `gen.close()` → `py_gen_close`

The existing `py_iterator_next` in `py_runtime.cpp` is updated to detect generator objects and call `py_gen_next` instead of the list/range/map path.

### Test Plan

```python
# test/py/test_py_generators.py

def counter(start, stop):
    i = start
    while i < stop:
        yield i
        i += 1

gen = counter(0, 5)
print(next(gen))    # 0
print(next(gen))    # 1
print(list(counter(3, 7)))  # [3, 4, 5, 6]

# send()
def accumulator():
    total = 0
    while True:
        value = yield total
        total += value

acc = accumulator()
next(acc)
print(acc.send(10))  # 10
print(acc.send(20))  # 30

# yield from
def chain(*iterables):
    for it in iterables:
        yield from it

print(list(chain([1, 2], [3, 4])))  # [1, 2, 3, 4]

# generator expression
squares = (x * x for x in range(6))
print(list(squares))   # [0, 1, 4, 9, 16, 25]
```

---

## 3. Phase B: `match`/`case` Pattern Matching

**Goal:** Enable Python 3.10+ structural pattern matching (`match`/`case`) with all standard pattern forms.

**Estimated effort:** ~750 LOC. Additions to `py_ast.hpp`, `build_py_ast.cpp`, `transpile_py_mir.cpp`, `py_runtime.cpp`. New helper: `py_pattern.cpp`.

### B1. Pattern Types

Python `match`/`case` supports seven pattern kinds:

| Pattern | Example | Description |
|---------|---------|-------------|
| Literal | `case 42:`, `case "hi":`, `case True:` | Exact value comparison |
| Capture | `case x:` | Bind subject to name |
| Wildcard | `case _:` | Always matches, no binding |
| OR | `case 0 \| 1 \| 2:` | Match any sub-pattern |
| Sequence | `case [x, y]:`, `case [first, *rest]:` | Match list/tuple by shape |
| Mapping | `case {"key": val}:` | Match dict entries |
| Class | `case Point(x=px, y=py):` | Match instance + extract attrs |
| Guard | `case x if x > 0:` | Pattern with `if` condition |

### B2. AST Additions

Add to `py_ast.hpp` and `build_py_ast.cpp`:

```c
// New node types
PY_AST_NODE_MATCH,              // match subject: cases...
PY_AST_NODE_CASE,               // case pattern [if guard]: body
PY_AST_NODE_PAT_LITERAL,        // literal pattern
PY_AST_NODE_PAT_CAPTURE,        // capture pattern (identifier)
PY_AST_NODE_PAT_WILDCARD,       // _ pattern
PY_AST_NODE_PAT_OR,             // pattern | pattern
PY_AST_NODE_PAT_SEQUENCE,       // [p1, p2, *rest]
PY_AST_NODE_PAT_MAPPING,        // {"k": p, **rest}
PY_AST_NODE_PAT_CLASS,          // ClassName(p1, attr=p2)
PY_AST_NODE_PAT_STAR,           // *name inside sequence pattern

typedef struct PyMatchNode {
    PyAstNode base;
    PyAstNode* subject;
    PyAstNode* cases;       // linked list of PyCaseNode
} PyMatchNode;

typedef struct PyCaseNode {
    PyAstNode base;
    PyAstNode* pattern;     // pattern AST
    PyAstNode* guard;       // optional if-guard expression
    PyAstNode* body;
} PyCaseNode;

typedef struct PyPatternNode {
    PyAstNode base;
    // discriminated by node_type:
    // PAT_LITERAL: literal field
    // PAT_CAPTURE: name field
    // PAT_OR: left/right
    // PAT_SEQUENCE: elements (linked list, may include PAT_STAR)
    // PAT_MAPPING: pairs (linked list of key-pattern), rest_name
    // PAT_CLASS: cls_name, positional patterns, keyword patterns
    PyAstNode* literal;
    String* name;
    PyAstNode* left;
    PyAstNode* right;
    PyAstNode* elements;
    String* rest_name;
    String* cls_name;
    PyAstNode* cls_positional;
    PyAstNode* cls_keywords;
} PyPatternNode;
```

**`build_py_ast.cpp` additions** (~200 LOC): `build_py_match_statement`, `build_py_case_clause`, `build_py_pattern` (recursive pattern parser).

### B3. Codegen

`pm_compile_statement` for `PY_AST_NODE_MATCH`:

1. Evaluate the `subject` expression into `reg_subject`.
2. For each case in order, emit a pattern-matching block:
   - Test the pattern against `reg_subject`.
   - On success: bind any capture variables, check guard, execute body, jump to `L_end_match`.
   - On failure: fall through to the next case.
3. `L_end_match`.

**Pattern matching codegen** — `pm_compile_pattern(mt, reg_subject, pattern, L_fail)`:

```
PAT_LITERAL:
    reg_cmp = CALL py_eq(reg_subject, <literal>)
    if not reg_cmp: JMP L_fail

PAT_CAPTURE:
    _py_name = reg_subject    // always succeeds, binds variable

PAT_WILDCARD:
    // no-op, always succeeds

PAT_OR:
    try left pattern → if succeeds, JMP L_ok
    try right pattern → if succeeds, JMP L_ok
    JMP L_fail
    L_ok:

PAT_SEQUENCE:
    reg_len = CALL py_builtin_len(reg_subject)
    // check length (with *rest: len >= fixed_count)
    // for each positional slot: reg_elem = CALL py_subscript_get(reg_subject, i)
    //   recurse: pm_compile_pattern(mt, reg_elem, sub_pattern, L_fail)
    // for *rest: bind slice to rest_name

PAT_MAPPING:
    // for each key: check CALL py_contains(reg_subject, key)
    //   if missing: JMP L_fail
    //   reg_val = CALL py_dict_get(reg_subject, key)
    //   recurse on value pattern
    // **rest: collect remaining keys

PAT_CLASS:
    // check isinstance(subject, cls)
    // extract __match_args__ or keyword-named attributes
    // recurse on sub-patterns
```

**New runtime helper** `py_match_sequence(Item subject)` — returns true if subject is a list, tuple, or any non-str/bytes iterable. Used by `PAT_SEQUENCE` to reject strings/bytes masquerading as sequences.

**Files:** `py_pattern.cpp` (~300 LOC), `build_py_ast.cpp` additions (~230 LOC), `transpile_py_mir.cpp` additions (~250 LOC), `py_runtime.cpp` (~50 LOC for `py_match_sequence`, `py_match_mapping_rest`).

### Test Plan

```python
# test/py/test_py_match.py
def classify(x):
    match x:
        case 0:
            return "zero"
        case int(n) if n > 0:
            return f"positive {n}"
        case str(s):
            return f"string: {s}"
        case [first, *rest]:
            return f"list starting with {first}"
        case {"action": action, "value": val}:
            return f"{action}={val}"
        case _:
            return "other"

print(classify(0))                        # zero
print(classify(42))                       # positive 42
print(classify("hi"))                     # string: hi
print(classify([1, 2, 3]))                # list starting with 1
print(classify({"action": "add", "value": 5}))  # add=5

# OR pattern
def http_error(status):
    match status:
        case 400 | 401 | 403:
            return "client error"
        case 500 | 502:
            return "server error"
        case _:
            return "other"

print(http_error(401))  # client error
print(http_error(502))  # server error
```

---

## 4. Phase C: Standard Library Module Stubs

**Goal:** Enable `import math`, `import os`, `import sys`, `import re`, `import json`, `import collections`, and other frequently used standard library modules via thin C-backed stubs.

**Estimated effort:** ~1,300 LOC. New files: `lambda/py/py_stdlib/py_mod_math.cpp`, `py_mod_os.cpp`, `py_mod_sys.cpp`, `py_mod_re.cpp`, `py_mod_json.cpp`, `py_mod_collections.cpp`, `py_mod_itertools.cpp`, `py_mod_functools.cpp`. Additions to `py_module.cpp` (from Phase E of v3).

### C1. Module Registry Extension

Extend `py_import_module` (from v3's `py_module.cpp`) with a built-in module table:

```c
// py_module.cpp additions
typedef Item (*PyBuiltinModuleInitFn)(void);

typedef struct PyBuiltinModuleEntry {
    const char* name;
    PyBuiltinModuleInitFn init;
} PyBuiltinModuleEntry;

static PyBuiltinModuleEntry builtin_modules[] = {
    { "math",        py_stdlib_math_init },
    { "os",          py_stdlib_os_init },
    { "os.path",     py_stdlib_ospath_init },
    { "sys",         py_stdlib_sys_init },
    { "re",          py_stdlib_re_init },
    { "json",        py_stdlib_json_init },
    { "collections", py_stdlib_collections_init },
    { "itertools",   py_stdlib_itertools_init },
    { "functools",   py_stdlib_functools_init },
    { "pathlib",     py_stdlib_pathlib_init },
    { "io",          py_stdlib_io_init },
    { "time",        py_stdlib_time_init },
    { "random",      py_stdlib_random_init },
    { "copy",        py_stdlib_copy_init },
    { NULL, NULL }
};
```

Each `py_stdlib_*_init()` returns a Map Item representing the module's namespace (just like a user module's export Map).

`py_import_module` checks the builtin table first, before trying filesystem resolution.

### C2. `math` Module (~150 LOC)

Wraps Lambda's existing numeric functions. All math functions already exist in the Lambda runtime (used by the Lambda language itself).

```c
// py_mod_math.cpp
Item py_stdlib_math_init() {
    Item mod = py_dict_new();
    // constants
    py_dict_set(mod, py_str("pi"),  py_float(M_PI));
    py_dict_set(mod, py_str("e"),   py_float(M_E));
    py_dict_set(mod, py_str("inf"), py_float(INFINITY));
    py_dict_set(mod, py_str("nan"), py_float(NAN));
    // functions: sin, cos, tan, asin, acos, atan, atan2,
    //            sqrt, log, log2, log10, exp, pow,
    //            floor, ceil, trunc, fabs, factorial,
    //            gcd, lcm (Python 3.9+), isnan, isinf, isfinite
    py_dict_set(mod, py_str("sqrt"), py_new_function((void*)py_math_sqrt, 1));
    // ... all others
    return mod;
}
```

Functions that Lambda already has: `sin`, `cos`, `tan`, `sqrt`, `log`, `floor`, `ceil`, `abs`, `pow`. Functions to add: `factorial`, `gcd`, `lcm`, `atan2`, `isnan`, `isinf`, `isfinite`, `comb`, `perm`.

### C3. `os` / `os.path` Module (~200 LOC)

Key subset:

```c
// os functions:
os.getcwd()       → py_os_getcwd
os.listdir(path)  → py_os_listdir
os.path.join(...)  → py_os_path_join
os.path.exists(p)  → py_os_path_exists
os.path.dirname(p) → py_os_path_dirname
os.path.basename(p)→ py_os_path_basename
os.path.abspath(p) → py_os_path_abspath
os.path.splitext(p)→ py_os_path_splitext
os.path.isfile(p)  → py_os_path_isfile
os.path.isdir(p)   → py_os_path_isdir
os.environ        → Map wrapping env vars (read-only)
```

Lambda already has path utilities in `lambda/path.c` — most functions are thin wrappers.

### C4. `sys` Module (~80 LOC)

```c
sys.argv          → List of command-line args (passed at module init)
sys.exit(code)    → py_raise(SystemExit)
sys.stdout.write(s) → stdout Output
sys.stderr.write(s) → stderr Output
sys.version       → version string
sys.platform      → "darwin" / "linux" / "win32"
```

`sys.argv` is populated at transpiler entry point from the Lambda CLI args.

### C5. `re` Module (~150 LOC)

Lambda already has re2 integration in `lambda/re2_wrapper.cpp`. The `re` module stub wraps it:

```c
re.compile(pattern, flags=0) → compiled regex object (Map with __pattern__, __flags__)
re.match(pattern, string)    → match object or None
re.search(pattern, string)   → match object or None
re.findall(pattern, string)  → list of strings
re.sub(pattern, repl, string, count=0) → string
re.split(pattern, string)    → list
```

Match object: a Map with `group()`, `groups()`, `start()`, `end()`, `span()` callable entries.

### C6. `json` Module (~100 LOC)

Lambda natively parses JSON (`lambda/input/input-json.cpp`) and formats it (`lambda/format/format-json.cpp`). The stub delegates:

```c
json.loads(s)          → py_json_loads (calls Lambda's JSON parser → Item)
json.dumps(obj, indent=None, sort_keys=False) → py_json_dumps
```

### C7. `collections` Module (~150 LOC)

```c
collections.OrderedDict     → regular dict (insertion-order guaranteed in Python 3.7+)
collections.defaultdict     → Map with __default_factory__ field; py_getattr detects
collections.Counter         → dict subclass with count semantics
collections.deque           → list with O(1) appendleft/popleft (backed by ArrayList)
collections.namedtuple      → dynamically creates a class with positional fields
```

`defaultdict` and `Counter` are implemented as real class objects (using v3's class system) created at module init time and returned in the module Map.

### C8. `itertools` Module (~150 LOC)

All `itertools` functions return generator objects (Phase A is a prerequisite):

```c
itertools.chain(*iterables)    → generator that chains
itertools.islice(it, stop)     → generator with slice
itertools.enumerate(it, start=0) → already a builtin, re-export
itertools.zip_longest(...)     → generator
itertools.product(...)         → Cartesian product generator
itertools.combinations(it, r)  → generator
itertools.permutations(it, r)  → generator
itertools.repeat(x, times=None)→ generator
itertools.count(start, step)   → infinite generator
itertools.takewhile(pred, it)  → generator
itertools.dropwhile(pred, it)  → generator
itertools.groupby(it, key)     → generator of (key, sub-iter) pairs
```

Phase A (generators) must be complete before this module is implemented.

### C9. `functools` Module (~100 LOC)

```c
functools.reduce(func, iterable, initializer=None) → already exists as builtin, re-export
functools.partial(func, *args, **kwargs)            → returns a new callable with pre-filled args
functools.lru_cache(maxsize=128)                    → memoization decorator
functools.wraps(wrapped)                            → copies __name__, __doc__ attrs
```

`functools.partial` creates a Map `{ "__is_partial__": true, "__func__": func, "__args__": args, "__kwargs__": kwargs }`. `py_call_function` detects and unwraps it (similar to bound-method detection in v3).

`functools.lru_cache` is a decorator factory; since decorator support exists in v3, this is ~50 LOC using a dict-backed cache keyed by hash(args).

### C10. `time` and `random` Modules (~100 LOC combined)

```c
// time
time.time()       → float (Unix timestamp)
time.sleep(secs)  → sleep (use platform usleep)
time.monotonic()  → monotonic float timer

// random
random.random()     → float in [0, 1)
random.randint(a,b) → int in [a, b]
random.choice(seq)  → random element
random.shuffle(lst) → shuffle in-place
random.seed(x)      → seed the RNG
```

### Test Plan

```python
# test/py/test_py_stdlib_math.py
import math
print(math.sqrt(16))        # 4.0
print(math.floor(3.7))      # 3
print(math.factorial(5))    # 120
print(math.gcd(12, 8))      # 4
print(round(math.pi, 4))    # 3.1416

# test/py/test_py_stdlib_os.py
import os
cwd = os.getcwd()
print(type(cwd) == str)     # True
print(os.path.join("a", "b", "c"))  # a/b/c
print(os.path.basename("/tmp/foo.txt"))  # foo.txt

# test/py/test_py_stdlib_re.py
import re
m = re.match(r"(\d+)-(\w+)", "42-hello")
print(m.group(1))              # 42
print(re.findall(r"\d+", "a1b2c3"))  # ['1', '2', '3']

# test/py/test_py_stdlib_json.py
import json
d = json.loads('{"a": 1, "b": [2, 3]}')
print(d["a"])                  # 1
print(json.dumps({"x": 42}))  # {"x": 42}

# test/py/test_py_stdlib_collections.py
from collections import defaultdict, Counter, deque
dd = defaultdict(int)
dd["x"] += 1
print(dd["x"])    # 1
c = Counter("abracadabra")
print(c["a"])     # 5
dq = deque([1, 2, 3])
dq.appendleft(0)
print(list(dq))   # [0, 1, 2, 3]
```

---

## 5. Phase D: `async`/`await` and Event Loop

**Goal:** Enable `async def`, `await`, `async for`, `async with`, and a minimal `asyncio` facade sufficient for practical async scripting.

**Estimated effort:** ~1,100 LOC. New files: `lambda/py/py_async.cpp`, `lambda/py/py_async.h`. Additions to `transpile_py_mir.cpp`, `py_runtime.cpp`, `py_ast.hpp`, `build_py_ast.cpp`, `sys_func_registry.c`.

**Prerequisite:** Phase A (generators) must be complete. Python coroutines are a specialization of generator objects.

### D1. Coroutine Model

Python coroutines (`async def`) are syntactic sugar over generator-based coroutines. Under CPython:
- `async def f()` creates a coroutine object (similar to a generator).
- `await expr` is equivalent to `yield from expr.__await__()`.
- The event loop drives coroutines by calling `send(None)` until `StopIteration`.

**Implementation strategy:** Reuse the generator state machine from Phase A. Coroutines are generator objects with an additional `__await__` method returning `self` and a `CO_COROUTINE` flag.

```c
// py_async.h additions:
bool py_is_coroutine(Item x);
Item py_coro_new(MIR_item_t resume_func, int local_count);
Item py_coro_await(Item coro);   // returns next yielded value or StopIteration
```

### D2. AST Changes

```c
// py_ast.hpp additions:
PY_AST_NODE_AWAIT,          // await expr (currently stripped — replace with proper node)

typedef struct PyAwaitNode {
    PyAstNode base;
    PyAstNode* value;
} PyAwaitNode;
```

`build_py_ast.cpp`: the existing `await` strip case (~line 1738) is replaced with `PyAwaitNode` construction.

**`async def` detection**: `pm_collect_functions_r` checks `TSNode` type for `async_function_definition`. Sets `fc->is_async = true` on collection.

```c
// PyFuncCollected additional field:
bool is_async;   // true if defined with async def
```

Async generator functions (`async def` + `yield`) are detected when both `is_async` and `is_generator` are true.

### D3. `await` Codegen

`await expr` in an `async def` body compiles as `yield from expr.__await__()`, using the generator state machine yield machinery from Phase A:

```
MIR for: result = await some_coro

    reg_iter = CALL py_getattr(some_coro, "__await__")
    reg_awaitable = CALL py_call_function(reg_iter, NULL, 0)
    // yield-from loop:
    L_await_loop:
        reg_val = CALL py_gen_send(reg_awaitable, _py_sent_value)
        IF py_is_stop_iteration(reg_val): JMP L_await_done
        // yield reg_val up to the event loop
        store _frame->state = <next_state>
        RET reg_val
    L_await_resume_<state>:
        JMP L_await_loop
    L_await_done:
        reg_result = CALL py_stop_iteration_value(reg_val)
```

### D4. `async for` and `async with`

**`async for x in aiter:`** desugars to:
```python
_it = aiter.__aiter__()
while True:
    try:
        x = await _it.__anext__()
    except StopAsyncIteration:
        break
    <body>
```

**`async with mgr as x:`** desugars to:
```python
x = await mgr.__aenter__()
try:
    <body>
except:
    if not await mgr.__aexit__(*sys.exc_info()):
        raise
else:
    await mgr.__aexit__(None, None, None)
```

Both are handled in `pm_compile_statement` by emitting the desugared form using existing exception and yield machinery.

### D5. Minimal `asyncio` Module (~200 LOC)

A single-threaded cooperative event loop sufficient for serial async scripts:

```c
// py_async.cpp: py_asyncio_run(coro)
// Runs the top-level coroutine to completion:
//   loop over py_coro_await(coro) until StopIteration
//   For awaitables that are Futures/Tasks: schedule and run to completion
//   For asyncio.sleep(0): yield control to next ready task

Item py_asyncio_run(Item coro);
Item py_asyncio_gather(Item* coros, int count);  // run concurrent coroutines to completion
Item py_asyncio_sleep(Item seconds);             // returns an awaitable
Item py_asyncio_create_task(Item coro);          // schedule a coroutine
```

The event loop is a simple queue of `(coroutine, send_value)` pairs processed in FIFO order. `asyncio.sleep(0)` yields to other tasks; `asyncio.sleep(n)` uses `usleep` (blocking, sufficient for scripting).

```c
// Module stub
Item py_stdlib_asyncio_init() {
    Item mod = py_dict_new();
    py_dict_set(mod, py_str("run"),         py_new_function((void*)py_asyncio_run, 1));
    py_dict_set(mod, py_str("gather"),      py_new_function((void*)py_asyncio_gather_variadic, -1));
    py_dict_set(mod, py_str("sleep"),       py_new_function((void*)py_asyncio_sleep, 1));
    py_dict_set(mod, py_str("create_task"), py_new_function((void*)py_asyncio_create_task, 1));
    return mod;
}
```

### Test Plan

```python
# test/py/test_py_async.py
import asyncio

async def fetch(url):
    await asyncio.sleep(0)  # yield control
    return f"data from {url}"

async def main():
    result = await fetch("http://example.com")
    print(result)    # data from http://example.com

asyncio.run(main())

# async for
async def aint_range(n):
    for i in range(n):
        await asyncio.sleep(0)
        yield i

async def consume():
    total = 0
    async for x in aint_range(5):
        total += x
    print(total)    # 10

asyncio.run(consume())

# gather
async def task(n):
    await asyncio.sleep(0)
    return n * 2

async def parallel():
    results = await asyncio.gather(task(1), task(2), task(3))
    print(results)  # [2, 4, 6]

asyncio.run(parallel())
```

---

## 6. Phase E: Full Package System

**Goal:** Enable `import pkg.submod`, `from pkg import name`, relative imports (`from . import x`, `from ..utils import y`), and correct handling of circular imports.

**Estimated effort:** ~550 LOC. Extensions to `lambda/py/py_module.cpp`, `lambda/py/py_module.h`. Additions to `transpile_py_mir.cpp`.

**Prerequisite:** Phase C (stdlib module stubs) for completeness, but can be implemented independently.

### E1. Package Resolution

Extend `py_import_module` to support dotted names and `__init__.py`:

```c
// py_module.cpp extended resolution order for "import pkg.submod":
// 1. Check built-in module table (Phase C)
// 2. Check module cache (by absolute path)
// 3. For "pkg.submod":
//    a. Look for ./pkg/__init__.py (if not already loaded, load it first)
//    b. Look for ./pkg/submod.py
//    c. Search sys.path entries (from py_stdlib_sys_init)
// 4. Load, compile, execute, cache

typedef struct PyPackageEntry {
    char name[256];         // dotted module name
    char filepath[512];     // absolute path to .py file
    Item exports;           // module namespace Map
    bool loading;           // cycle detection flag
} PyPackageEntry;
```

`__init__.py` is loaded once per package and its exports are merged as the package namespace. Submodule attributes are added to the parent package Map after loading.

### E2. Relative Imports

`from . import x` and `from .. import y` require knowing the package of the current module. The transpiler passes `current_module_name` alongside `filename` to `py_import_module`.

```c
// py_module.h addition:
Item py_import_module_relative(EvalContext* ctx, const char* dots,
                               const char* name, const char* from_module_name);
```

Resolution: strip `len(dots)` components from `from_module_name`'s dotted path, then resolve `name` relative to that package directory.

**Transpiler change** in `pm_compile_statement` for `PY_AST_NODE_IMPORT_FROM`: detect when `from_module` starts with `.` characters; emit `py_import_module_relative` call instead of `py_import_module`.

### E3. Circular Import Handling

Set `entry->loading = true` before executing a module. If `py_import_module` is called for a module already being loaded:
1. Return a partially-initialized module Map (whatever has been defined so far).
2. Log a warning via `log_info()`.
3. Rely on the caller using the module lazily (attribute access at call time, not import time).

This covers the most common circular import pattern (where modules use each other's functions, not each other's module-level values at import time).

### E4. `sys.path` Support

After Phase C adds the `sys` module, extend it with a mutable `sys.path` list. `py_import_module` searches each directory in `sys.path` after the current file's directory.

Users can prepend directories:
```python
import sys
sys.path.insert(0, "/path/to/mylibrary")
import mymodule
```

### Test Plan

```python
# test/py/pkg/__init__.py
from .utils import greet

# test/py/pkg/utils.py
def greet(name):
    return f"Hello, {name}!"

def helper():
    return 42

# test/py/test_py_packages.py
import pkg
print(pkg.greet("World"))     # Hello, World!

from pkg import greet
print(greet("Lambda"))        # Hello, Lambda!

from pkg.utils import helper
print(helper())               # 42

# relative import within package:
# pkg/__init__.py uses: from .utils import greet (tested above)
```

---

## 7. Phase F: Advanced OOP — Metaclasses, Full Descriptors, `@prop.setter`

**Goal:** Complete the class system with `@property` setter/deleter, full descriptor protocol (`__get__`/`__set__`/`__delete__`), `__init_subclass__`, `__class_getitem__`, and basic metaclass support.

**Estimated effort:** ~650 LOC. Extensions to `lambda/py/py_class.cpp`, `lambda/py/py_class.h`, `py_runtime.cpp`, `transpile_py_mir.cpp`.

### F1. `@property` Setter and Deleter

v3 implemented `@property` getter only. Add setter/deleter support by:

1. Extending the property Map representation:
   ```c
   // property Map: { "__is_property__": true, "__get__": fget, "__set__": fset,
   //                 "__delete__": fdel, "__doc__": doc }
   ```

2. Adding `py_property_setter(prop, fset)` and `py_property_deleter(prop, fdel)` runtime functions.

3. In `pm_apply_decorators`, detecting `@prop.setter` (an attribute access, not a bare identifier):
   ```python
   @some_prop.setter
   def some_prop(self, value): ...
   ```
   This is a method decorator where the decorator expression is `Attribute(name="setter", object=Identifier("some_prop"))`. The transpiler emits:
   ```
   reg_prop = CALL py_property_setter(_py_some_prop, pyf_ClassName__some_prop_setter)
   // update the class dict entry
   ```

4. `py_setattr` on an instance: detect `__is_property__` in the class dict; if `__set__` is present, call it instead of setting the instance field directly.

5. `py_getattr` on an instance: already detects `__is_property__` in v3. Ensure deletion path calls `__delete__`.

**Files:** `py_class.cpp` additions (~150 LOC), `transpile_py_mir.cpp` additions (~80 LOC) for `@prop.setter`/`@prop.deleter` decorator detection.

### F2. Full Descriptor Protocol

The descriptor protocol (`__get__`, `__set__`, `__delete__`) generalizes `@property` to arbitrary descriptor objects (used by `classmethod`, `staticmethod`, and user-defined descriptors).

**Data descriptors** (have both `__get__` and `__set__`) take priority over instance `__dict__`.  
**Non-data descriptors** (have only `__get__`) yield to instance `__dict__`.

Extend `py_getattr` and `py_setattr` in `py_runtime.cpp`:

```
py_getattr(obj, name):
    1. Look in obj.__class__.__mro__ for a data descriptor (has __get__ AND __set__)
       → if found: call descriptor.__get__(obj, type(obj))  [highest priority]
    2. Look in obj's instance dict
       → if found: return value
    3. Look in obj.__class__.__mro__ for a non-data descriptor (has __get__ only)
       → if found: call descriptor.__get__(obj, type(obj))
    4. Return instance dict value (if any)
    5. AttributeError
```

**`classmethod` and `staticmethod`** are implemented as descriptor objects:

```c
// py_class.cpp additions:
Item py_builtin_classmethod(Item func);  // descriptor that passes cls instead of self
Item py_builtin_staticmethod(Item func); // descriptor that returns raw function
```

Both are registered in `sys_func_registry.c` and in `py_init_builtins_classes()`.

**Files:** `py_class.cpp` additions (~150 LOC for descriptor dispatch, `classmethod`, `staticmethod`), `py_runtime.cpp` changes (~80 LOC in `py_getattr`/`py_setattr`).

### F3. `__init_subclass__`

Called on a class's bases whenever the class is subclassed:

```python
class Plugin:
    def __init_subclass__(cls, /, **kwargs):
        super().__init_subclass__(**kwargs)
        Plugin.registry.append(cls)
    registry = []

class MyPlugin(Plugin):    # triggers Plugin.__init_subclass__
    pass
```

In `py_class_new` (in `py_class.cpp`), after computing the MRO, walk the base classes and call `__init_subclass__` on each base that defines it:

```c
Item py_class_new(Item name, Item bases, Item methods) {
    // ... existing class creation ...
    // after creation:
    for each base in bases:
        Item fn = py_mro_lookup(base, "__init_subclass__");
        if (!is_item_null(fn)):
            // call base.__init_subclass__(new_class) with any kwargs from class keyword args
            py_call_function(fn, &new_class, 1);
    return new_class;
}
```

**Transpiler change**: the `CLASS_DEF` node in Tree-sitter can carry keyword arguments (e.g. `class Foo(Base, metaclass=Meta)`). Extract these and pass as `**kwargs` to `__init_subclass__`.

**Files:** `py_class.cpp` additions (~80 LOC), `transpile_py_mir.cpp` additions (~50 LOC for keyword base extraction).

### F4. `__class_getitem__`

Allows classes to support `ClassName[T]` syntax (used by type annotations like `list[int]`, `dict[str, int]`):

```python
class MyGeneric:
    def __class_getitem__(cls, item):
        return f"MyGeneric[{item}]"

print(MyGeneric[int])  # MyGeneric[<class 'int'>]
```

In `py_subscript_get` (`py_runtime.cpp`): when the object is a class (has `__is_class__`), look up `__class_getitem__` in the class dict and call it:

```c
Item py_subscript_get(Item object, Item key) {
    // existing list/dict/string/tuple paths...
    // new: class subscript
    if (py_is_class(object)) {
        Item fn = py_mro_lookup(object, "__class_getitem__");
        if (!is_item_null(fn)) {
            Item args[2] = { object, key };
            return py_call_function(fn, args, 2);
        }
    }
    // ...
}
```

For built-in types (`list[int]`, `dict[str, int]`), return a generic alias Map `{ "__origin__": cls, "__args__": [T] }` — sufficient for isinstance checks with the standard library's typing module stubs.

**Files:** `py_runtime.cpp` additions (~60 LOC), `py_class.cpp` additions (~40 LOC for `_GenericAlias` construction).

### F5. Basic Metaclass Support

Metaclasses intercept class creation:

```python
class Meta(type):
    def __new__(mcs, name, bases, namespace):
        cls = super().__new__(mcs, name, bases, namespace)
        return cls
```

Full metaclass support (descriptor inheritance, `type.__prepare__`, `__instancecheck__`) is complex. v4 targets the 80% case:

1. Detect `metaclass=Meta` in class definition keyword arguments.
2. If present, instead of calling `py_class_new` directly, call `Meta.__new__(Meta, name, bases, methods)`.
3. `type.__new__` is the default — delegates to `py_class_new`.
4. Custom metaclasses can override `__new__` and/or `__init__` to modify the class.

**Limitation:** `__prepare__` (class namespace customization before body execution) requires significant transpiler changes (the body must be compiled into a user-supplied dict, not a fresh Map). Deferred to v5.

**Files:** `py_class.cpp` additions (~100 LOC), `transpile_py_mir.cpp` additions (~70 LOC for keyword base arg extraction and metaclass dispatch).

### Test Plan

```python
# test/py/test_py_descriptors.py
class Temperature:
    def __init__(self, celsius):
        self._celsius = celsius

    @property
    def celsius(self):
        return self._celsius

    @celsius.setter
    def celsius(self, value):
        if value < -273.15:
            raise ValueError("Temperature below absolute zero")
        self._celsius = value

    @property
    def fahrenheit(self):
        return self._celsius * 9/5 + 32

t = Temperature(25)
print(t.celsius)      # 25
print(t.fahrenheit)   # 77.0
t.celsius = 100
print(t.fahrenheit)   # 212.0

# test/py/test_py_metaclass.py
class SingletonMeta(type):
    _instances = {}
    def __call__(cls, *args, **kwargs):
        if cls not in cls._instances:
            cls._instances[cls] = super().__call__(*args, **kwargs)
        return cls._instances[cls]

class Database(metaclass=SingletonMeta):
    def __init__(self):
        self.connection = "connected"

db1 = Database()
db2 = Database()
print(db1 is db2)     # True

# test/py/test_py_init_subclass.py
class PluginBase:
    _registry = []
    def __init_subclass__(cls, **kwargs):
        super().__init_subclass__(**kwargs)
        PluginBase._registry.append(cls.__name__)

class PluginA(PluginBase): pass
class PluginB(PluginBase): pass

print(PluginBase._registry)  # ['PluginA', 'PluginB']
```

---

## 8. Implementation Order and Dependencies

```
Phase A: Generators/yield     — independent of B–F; prerequisite for D and C(itertools)
    │
    ├── Phase B: match/case   — independent of all others
    │
    ├── Phase C: stdlib       — independent; itertools needs A
    │       │
    │       └── Phase E: Packages  — extends C's module registry; needs v3 Phase E base
    │
    └── Phase D: async/await  — depends on A (coroutines = generators)
            │
            └── Phase C: asyncio — part of C's stdlib stubs, needs D
                
Phase F: Advanced OOP         — independent; extends v3 class system
```

Recommended implementation sequence:
1. **F1** (`@prop.setter/deleter`) — smallest, highest user impact, no dependencies
2. **B** (pattern matching) — self-contained, high value
3. **A** (generators) — large but unblocked; unlocks D and C(itertools)
4. **C** (stdlib stubs, except itertools/asyncio) — unblocked after A
5. **D** (async/await) — requires A
6. **C** (itertools/asyncio stubs) — requires A and D
7. **E** (packages) — builds on C's module infrastructure
8. **F2–F5** (descriptors, `__init_subclass__`, metaclasses) — complete OOP

---

## 9. LOC Estimate Summary

| Phase | File(s) | Estimated LOC | Status | Notes |
|-------|---------|--------------|--------|-------|
| A | `py_generator.cpp` (new) | ~450 | ⏳ pending | Generator state machine, frame alloc, yield-from loop |
| A | `py_generator.h` (new) | ~80 | ⏳ pending | C API: gen_new, gen_next, gen_send, gen_throw, gen_close |
| A | `py_runtime.cpp` | +150 | ⏳ pending | iterator dispatch for generators, StopIteration value |
| A | `transpile_py_mir.cpp` | +350 | ⏳ pending | is_generator flag, state machine codegen, genexpr synthesis |
| A | `build_py_ast.cpp` | +50 | ⏳ pending | PyYieldNode construction replacing strip |
| A | `py_ast.hpp` | +30 | ⏳ pending | `YIELD`, `YIELD_FROM` node types |
| B | `py_pattern.cpp` (new) | ~300 | ⏳ pending | Pattern match codegen: all 7 pattern kinds |
| B | `build_py_ast.cpp` | +230 | ⏳ pending | `build_py_match_statement`, `build_py_pattern` |
| B | `transpile_py_mir.cpp` | +250 | ⏳ pending | Match statement codegen |
| B | `py_ast.hpp` | +60 | ⏳ pending | Match/case + pattern node types |
| B | `py_runtime.cpp` | +50 | ⏳ pending | `py_match_sequence`, `py_match_mapping_rest` |
| C | `py_stdlib/py_mod_math.cpp` (new) | ~150 | ⏳ pending | 25+ math functions |
| C | `py_stdlib/py_mod_os.cpp` (new) | ~200 | ⏳ pending | os + os.path |
| C | `py_stdlib/py_mod_sys.cpp` (new) | ~80 | ⏳ pending | argv, exit, stdout/stderr |
| C | `py_stdlib/py_mod_re.cpp` (new) | ~150 | ⏳ pending | re2 wrapper facade |
| C | `py_stdlib/py_mod_json.cpp` (new) | ~100 | ⏳ pending | Lambda JSON delegate |
| C | `py_stdlib/py_mod_collections.cpp` (new) | ~150 | ⏳ pending | defaultdict, Counter, deque, namedtuple |
| C | `py_stdlib/py_mod_itertools.cpp` (new) | ~150 | ⏳ pending | Generators (needs Phase A) |
| C | `py_stdlib/py_mod_functools.cpp` (new) | ~100 | ⏳ pending | reduce, partial, lru_cache, wraps |
| C | `py_stdlib/py_mod_time.cpp` (new) | ~60 | ⏳ pending | time, sleep, monotonic |
| C | `py_stdlib/py_mod_random.cpp` (new) | ~80 | ⏳ pending | random, randint, choice, shuffle, seed |
| C | `py_module.cpp` | +100 | ⏳ pending | Built-in module table, dispatch |
| D | `py_async.cpp` (new) | ~450 | ⏳ pending | Coroutine machinery, event loop, gather, sleep, create_task |
| D | `py_async.h` (new) | ~60 | ⏳ pending | C API for async/await |
| D | `transpile_py_mir.cpp` | +250 | ⏳ pending | is_async flag, await codegen, async for/with desugaring |
| D | `build_py_ast.cpp` | +30 | ⏳ pending | PyAwaitNode replacing strip |
| D | `py_ast.hpp` | +20 | ⏳ pending | `AWAIT` node type |
| E | `py_module.cpp` | +300 | ⏳ pending | Package resolution, __init__.py, relative imports, sys.path |
| E | `py_module.h` | +50 | ⏳ pending | PyPackageEntry, relative import API |
| E | `transpile_py_mir.cpp` | +100 | ⏳ pending | Relative import codegen |
| F | `py_class.cpp` | +350 | ⏳ pending | @prop.setter/deleter, classmethod, staticmethod, descriptor dispatch, __init_subclass__, __class_getitem__, metaclass |
| F | `transpile_py_mir.cpp` | +200 | ⏳ pending | @prop.setter detection, metaclass keyword arg, __class_getitem__ call |
| F | `py_runtime.cpp` | +130 | ⏳ pending | Descriptor protocol in py_getattr/py_setattr, class subscript |
| Tests | `test/py/*.py` + `test/py/*.txt` | ~450 | ⏳ pending | Generators, match, stdlib, async, packages, descriptors, metaclass |
| **Total** | | **~5,560** | ⏳ all pending | |

**v4 total projection:** ~14,400 (v3) + ~5,560 (v4) = **~19,960 LOC**

---

## 10. Testing Strategy

### Regression

All existing tests in `test/py/` must continue to pass after each phase. Run `make test-lambda-baseline` after each phase merge.

### New Test Files

| Test file | Phase | Key coverage |
|-----------|-------|-------------|
| `test_py_generators.py` | A | `yield`, `next()`, `send()`, `yield from`, generator expression |
| `test_py_generators_advanced.py` | A | `throw()`, `close()`, nested generators, infinite sequences |
| `test_py_match.py` | B | All 7 pattern kinds, guard, OR pattern |
| `test_py_match_class.py` | B | Class patterns, `__match_args__` |
| `test_py_stdlib_math.py` | C | All math module functions |
| `test_py_stdlib_os.py` | C | os, os.path functions |
| `test_py_stdlib_re.py` | C | re.match, re.search, re.findall, re.sub, re.split |
| `test_py_stdlib_json.py` | C | json.loads, json.dumps, nested structures |
| `test_py_stdlib_collections.py` | C | defaultdict, Counter, deque, namedtuple |
| `test_py_stdlib_itertools.py` | C+A | chain, islice, product, combinations |
| `test_py_stdlib_functools.py` | C | partial, lru_cache, wraps |
| `test_py_async.py` | D | `async def`, `await`, `asyncio.run()` |
| `test_py_async_gather.py` | D | `asyncio.gather`, `async for`, `async with` |
| `test_py_packages.py` | E | `import pkg.submod`, `from pkg import name`, relative imports |
| `test_py_descriptors.py` | F | `@property` setter/deleter, `classmethod`, `staticmethod` |
| `test_py_metaclass.py` | F | Basic metaclass `__new__`, `__call__`, singleton pattern |
| `test_py_init_subclass.py` | F | `__init_subclass__`, plugin registry pattern |
| `test_py_class_getitem.py` | F | `__class_getitem__`, generic type subscript |
