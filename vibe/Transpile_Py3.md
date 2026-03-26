# Python Transpiler v3: Classes, Context Managers, Decorators, `**kwargs`, and Imports

## 1. Executive Summary

LambdaPy v2 closed all core language gaps for single-file procedural and functional Python (~10.9K LOC). The five features deferred from v1/v2 that remain missing are: **class definitions**, **`with` statement**, **decorators**, **`**kwargs`**, and a **basic import system**. This proposal targets those in priority order.

> **Implementation Status:** Not yet started. This is the v3 planning document.

### Architecture Position

```
v1:  Python AST → MIR IR → native                     (done, ~7.8K LOC)
       Core expressions, control flow, functions, 29 builtins
v2:  Close core language gaps                          (✅ COMPLETE, ~10.9K LOC)
       Default/keyword args, slicing, f-string specs, comprehensions,
       closures, exceptions, *args, iter/next, open(), 40+ builtins
v3:  OOP and module system                             (this doc, target ~15K LOC)
       Phase A: Class system (class, inheritance, super, dunders)
       Phase B: with statement (context managers)
       Phase C: Decorators
       Phase D: **kwargs (complete calling convention)
       Phase E: Single-file imports
```

### Current vs Target Coverage

| Metric | v2 (actual) | v3 (target) |
|--------|-------------|-------------|
| Classes (`class`, `__init__`, methods, `self`) | ❌ AST parsed, no codegen | ✅ Single + multiple inheritance |
| `super()` | ❌ | ✅ Via MRO proxy |
| `isinstance` / `issubclass` | Partial (string-name matching) | ✅ Full MRO-based |
| Common dunder methods | ❌ | ✅ `__init__`, `__str__`, `__repr__`, `__len__`, `__eq__`, `__add__`, `__iter__`, `__getitem__`, `__bool__`, `__enter__`, `__exit__` |
| `with` statement | ❌ AST parsed, no codegen | ✅ Via `__enter__`/`__exit__` |
| Decorators | ❌ | ✅ Function and class decorators |
| `**kwargs` | ❌ DICT_SPLAT_PARAMETER skipped | ✅ Full pack/unpack |
| `import` / `from … import` | No-op | ✅ Single-file imports |
| Estimated new LOC | — | ~4,100 |

### Non-Goals (v3)

- `async`/`await`, generators, `yield` (deferred to v4)
- `match`/`case` pattern matching (deferred to v4)
- Metaclasses, `__init_subclass__`, `__class_getitem__` (deferred to v4)
- Arbitrary-precision integers
- C extension imports (`import numpy`)
- Full package/`__init__.py` resolution (single-file only in v3)
- Descriptor protocol beyond methods and properties

---

## 2. Phase A: Class System

**Goal:** Enable `class Foo(Bar): ...` with `__init__`, instance methods, class attributes, inheritance, `super()`, `isinstance`, and the most common dunder methods.

**Estimated effort:** ~2,200 LOC (new `py_class.cpp` + `py_class.h`, transpiler additions, runtime updates).

### A1. Class Object Representation

Python class objects and instances are both `Map` values in Lambda. Two conventions distinguish them:

| Object | Marker field | Key fields |
|--------|-------------|-----------|
| Class object | `"__is_class__"` = `b2it(true)` | `"__name__"`, `"__bases__"`, `"__mro__"`, plus method/class-attr entries |
| Instance | `"__class__"` = class Map Item | instance attribute entries |

No new `TypeId` is added. This keeps everything within the existing Map infrastructure and avoids changes to `lambda.h`.

**New file: `lambda/py/py_class.cpp`** (~700 LOC) + **`lambda/py/py_class.h`** (~80 LOC):

```c
// lambda/py/py_class.h (excerpt)
Item py_class_new(Item name, Item bases, Item methods);
Item py_compute_mro(Item cls);
Item py_new_instance(Item cls);
Item py_super(Item type, Item obj);
Item py_isinstance_v3(Item obj, Item cls);
Item py_issubclass_v3(Item sub, Item cls);
Item py_bind_method(Item func, Item self);
bool py_is_class(Item x);
bool py_is_instance(Item x);
Item py_get_class(Item obj);         // returns __class__ or NULL
Item py_mro_lookup(Item cls, Item name); // walk MRO for a name
```

**`py_class_new`** creates the class Map, copies all `methods` entries into it (so the class Map IS the class dict), then calls `py_compute_mro` to pre-compute and store `__mro__`:

```c
Item py_class_new(Item name, Item bases, Item methods) {
    // alloc new Map
    // copy each entry from methods into class Map
    // set class Map["__name__"]     = name
    // set class Map["__bases__"]    = bases (tuple/list)
    // set class Map["__is_class__"] = b2it(true)
    // set class Map["__mro__"]      = py_compute_mro(class_obj)
    return class_obj;
}
```

**`py_compute_mro`** — C3 linearization. For single-base classes (the common case) this is trivial. The full C3 algorithm handles diamond inheritance:

```c
Item py_compute_mro(Item cls) {
    // base case: no bases → [cls]
    // single base: [cls] + base.__mro__
    // multiple bases: C3 merge algorithm
    //   result = [cls] + merge(base1.__mro__, base2.__mro__, ..., [base1, base2, ...])
    //   merge: take head of first list if it doesn't appear in tail of any other
    //          repeat until all lists exhausted (or raise TypeError on conflict)
}
```

**`py_new_instance`** allocates a Map with `__class__` pointing to the class:

```c
Item py_new_instance(Item cls) {
    Map* inst = (Map*)pool_calloc(heap->pool, sizeof(Map) + INITIAL_SHAPE_SLOTS);
    inst->type_id = LMD_TYPE_MAP;
    map_set_field(inst, "__class__", cls);
    return it((Container*)inst);
}
```

**`py_bind_method`** wraps a function+self pair into a bound-method Map:

```c
Item py_bind_method(Item func, Item self) {
    // return Map { "__is_bound_method__": true, "__func__": func, "__self__": self }
}
```

### A2. `py_getattr` / `py_setattr` Updates

The existing `py_getattr` and `py_setattr` handle plain Map lookups. They need two extensions:

1. **Class attribute lookup:** If the field is not found in the instance dict, walk `__mro__` to find it in a class dict. If found and it's a function, return a bound method via `py_bind_method(func, instance)`.

2. **Class `__getattr__`:** If lookup fails and the class defines `__getattr__`, call it (deferred to v3.1).

Updated dispatch in `py_getattr`:

```
py_getattr(obj, name):
    1. if obj has field `name` directly → return it
    2. if obj has `__class__`:           // it's an instance
       cls = map_get(obj, "__class__")
       result = py_mro_lookup(cls, name)
       if result found:
           if py_is_callable(result): return py_bind_method(result, obj)
           else:                      return result   // class attr, no binding
    3. if obj has `__is_class__`:        // class attribute access
       result = py_mro_lookup(obj, name) // includes bases
       if result found: return result
    4. → return ItemNull (attribute error)
```

**`py_setattr`** always sets on the instance dict (never the class dict) when `obj` is an instance. To set class attributes, users access the class object directly.

**Files:** `py_runtime.cpp` additions (~200 LOC for updated `py_getattr`/`py_setattr`, MRO walk, bound method creation).

### A3. `py_call_function` Update for Bound Methods and Class Calls

`py_call_function` needs two new dispatch paths:

```c
Item py_call_function(Item callee, Item* args, int argc) {
    // 1. Bound method: unwrap and prepend __self__
    if (map_get_bool(callee, "__is_bound_method__")) {
        Item func = map_get_str(callee, "__func__");
        Item self = map_get_str(callee, "__self__");
        // build new_args = [self] + args
        return py_call_function(func, new_args, argc + 1);
    }
    // 2. Class call (construction): allocate instance + call __init__
    if (py_is_class(callee)) {
        Item inst = py_new_instance(callee);
        Item init_func = py_mro_lookup(callee, "__init__");
        if (!is_null(init_func)) {
            // call __init__(inst, args...)
            py_call_function_with_self(init_func, inst, args, argc);
        }
        return inst;
    }
    // 3. Existing function/closure path
    ...
}
```

**Files:** `py_runtime.cpp` additions (~150 LOC).

### A4. Transpiler: Class Definition Compilation

**New transpiler struct** added to `PyMirTranspiler`:

```c
struct PyClassCollected {
    PyClassDefNode* node;
    char name[128];          // "ClassName"
    char mir_prefix[160];    // "pyf_ClassName__" for method name mangling
    int method_indices[32];  // indices into func_entries[] for each method
    int method_count;
};

// added to PyMirTranspiler:
PyClassCollected class_entries[64];
int class_count;
```

**Phase 1.0 extension — class collection** (`pm_collect_functions_r`):

When `PY_AST_NODE_CLASS_DEF` is encountered:
1. Register the class in `class_entries[]`.
2. Recurse into the class body, collecting each `def` as a function entry. For each method:
   - Prefix the MIR function name: `pyf_ClassName___init__`, `pyf_ClassName__speak`, etc.
   - Mark `fc->is_method = true` and `fc->class_index = class_idx`.
   - The first parameter is always `self` (added implicitly if the user wrote it, or enforced).

**Phase 2 — class definition codegen** (`pm_compile_statement` for `PY_AST_NODE_CLASS_DEF`):

```
Python:
    class Dog(Animal):
        species = "Canine"

        def __init__(self, name, breed):
            super().__init__(name)
            self.breed = breed

        def speak(self):
            return f"{self.name} barks ({self.breed})"

MIR codegen at the class definition site:
    // methods already compiled as pyf_Dog___init__, pyf_Dog__speak

    // 1. build methods dict
    reg_methods = CALL py_dict_new()
    CALL py_dict_set(reg_methods, "species", <boxed "Canine">)
    CALL py_dict_set(reg_methods, "__init__", &pyf_Dog___init__)
    CALL py_dict_set(reg_methods, "speak",    &pyf_Dog__speak)

    // 2. build bases list
    reg_bases = CALL py_list_new()
    CALL py_list_append(reg_bases, _py_Animal)   // resolved in current scope

    // 3. create class object
    _py_Dog = CALL py_class_new("Dog", reg_bases, reg_methods)
```

**`ClassName(args)` at call sites:**

`pm_transpile_call` already resolves known user functions via `func_entries`. It must also check `class_entries`. When the callee name matches a class:

```
// _py_Dog("Rex", "Husky")  →
reg_inst = CALL py_new_instance(_py_Dog)
CALL pyf_Dog___init__(reg_inst, <"Rex">, <"Husky">)
// result = reg_inst
```

This is the compile-time fast path. For dynamic class calls (class passed as a value), `py_call_function`'s runtime check (A3) handles it.

**Files:** `transpile_py_mir.cpp` additions (~700 LOC for class collection, class codegen, call-site detection, `super()` transformation).

### A5. `super()` Compilation

Python 3 `super()` with no arguments uses two implicit values: the class being defined (`__class__`) and `self` (first parameter of the method). The transpiler injects this at compile time.

When compiling a method inside a class (`fc->is_method == true`), the transpiler:
1. Implicitly adds a `__class__` cell variable holding the class MIR item reference.
2. At `super()` call sites, replaces with `py_super(__class__, self)`.

```
Python: super().__init__(name)

MIR:
    reg_super = CALL py_super(_py_Dog, _py_self)
    reg_init  = CALL py_getattr(reg_super, "__init__")
    CALL py_call_function(reg_init, _py_name, ...)
```

`py_super(type, obj)` returns a proxy Map:
```c
// { "__is_super__": true, "__type__": type, "__obj__": obj }
```

`py_getattr` on a super proxy starts MRO lookup at the class *after* `type` in `obj.__class__.__mro__`.

**Files:** `py_class.cpp` for `py_super` (~80 LOC), transpiler detection of super() (~50 LOC).

### A6. Dunder Methods

The following dunder methods are wired up in v3. "Wired up" means the corresponding runtime operator checks for and calls the dunder when the operand is a class instance.

| Dunder | Triggered by | Runtime function updated |
|--------|-------------|--------------------------|
| `__init__` | `ClassName(args)` | `py_call_function` (A3) |
| `__str__` | `str(x)`, `print(x)` | `py_to_str` |
| `__repr__` | `repr(x)` | `py_builtin_repr` |
| `__len__` | `len(x)` | `py_builtin_len` |
| `__bool__` | `if x:`, `bool(x)` | `py_is_truthy` |
| `__eq__` | `==`, `!=` | `py_eq`, `py_ne` |
| `__lt__`, `__le__`, `__gt__`, `__ge__` | `<`, `<=`, `>`, `>=` | `py_lt`, `py_le`, `py_gt`, `py_ge` |
| `__add__`, `__radd__` | `+` | `py_add` |
| `__sub__`, `__mul__`, `__truediv__`, `__floordiv__`, `__mod__` | arithmetic ops | respective `py_*` functions |
| `__iter__` | `for x in obj`, `iter(x)` | `py_get_iterator` |
| `__next__` | `next(x)` | `py_iterator_next` |
| `__getitem__` | `obj[key]` | `py_subscript_get` |
| `__setitem__` | `obj[key] = val` | `py_subscript_set` |
| `__contains__` | `x in obj` | `py_contains` |
| `__enter__` | `with obj as v:` | `py_context_enter` (Phase B) |
| `__exit__` | `with` block end | `py_context_exit` (Phase B) |

The check pattern in each operator is:
```c
Item py_add(Item a, Item b) {
    // existing fast paths for int/float/string...
    // fallback for class instances:
    if (py_is_instance(a)) {
        Item dunder = py_mro_lookup(py_get_class(a), "__add__");
        if (!is_null(dunder)) return py_call_function(py_bind_method(dunder, a), &b, 1);
        Item rdunder = py_mro_lookup(py_get_class(b), "__radd__");
        if (!is_null(rdunder)) return py_call_function(py_bind_method(rdunder, b), &a, 1);
    }
    // type error fallback
    ...
}
```

**Files:** `py_runtime.cpp` additions (~150 LOC for dunder dispatch in 12 operator functions), `py_builtins.cpp` additions (~50 LOC for `len`, `str`, `repr`, `bool`, `isinstance`, `issubclass` updates).

### A7. `isinstance` and `issubclass` (Full MRO-Based)

Currently `py_isinstance` (in `py_builtins.cpp`) does a string-name match on the exception type. v3 replaces this with a proper MRO walk:

```c
// py_isinstance_v3(obj, cls):
//   if obj has __class__: walk __class__.__mro__ and check for cls pointer equality
//   else: check TypeId-based type matches (backward compat for built-in types)

// py_issubclass_v3(sub, cls):
//   walk sub.__mro__ and check for cls pointer equality
```

The built-in `isinstance(x, TypeError)` used in exception handling (Phase E) also needs updating: when `cls` is a string (legacy path from v1/v2), fall back to the old string-name comparison.

**Files:** `py_class.cpp` (~100 LOC), `py_builtins.cpp` update (~50 LOC).

### A8. Built-in `object` Class

Python's `object` is the implicit base class for all classes without an explicit base. The transpiler emits `_py_object` as the default base when the `bases` list is empty. `_py_object` is created at module init time:

```c
// py_class.cpp: called from py_module_init()
void py_init_builtins_classes() {
    // create 'object' class: empty MRO = [object], no bases
    _py_object_class = py_class_new(it_str("object"), py_list_new(), py_dict_new());
    // create built-in exception classes: Exception, ValueError, TypeError, etc.
    _py_Exception_class = py_class_new(..., [_py_object_class], ...);
    ...
}
```

This also enables the exception hierarchy to be real class objects instead of string sentinels.

### Test Plan

```python
# test/py/test_py_classes.py
class Animal:
    count = 0

    def __init__(self, name):
        self.name = name
        Animal.count += 1

    def speak(self):
        return f"{self.name} says ..."

    def __str__(self):
        return f"Animal({self.name})"

    def __repr__(self):
        return f"Animal(name={self.name!r})"

class Dog(Animal):
    def __init__(self, name, breed):
        super().__init__(name)
        self.breed = breed

    def speak(self):
        return f"{self.name} barks"

    def __len__(self):
        return len(self.name)

d = Dog("Rex", "Husky")
print(d.speak())              # Rex barks
print(d.name)                 # Rex
print(str(d))                 # Animal(Rex)
print(len(d))                 # 3
print(isinstance(d, Dog))     # True
print(isinstance(d, Animal))  # True
print(Animal.count)           # 1
```

```python
# test/py/test_py_multiple_inherit.py
class A:
    def method(self): return "A"

class B(A):
    def method(self): return "B"

class C(A):
    def method(self): return "C"

class D(B, C):
    pass

d = D()
print(d.method())             # B  (C3: D→B→C→A)
print(D.__mro__)              # [D, B, C, A, object]
```

---

## 3. Phase B: `with` Statement

**Goal:** Enable `with expr as var: body`. Requires dispatching `__enter__`/`__exit__` on the context manager object.

**Estimated effort:** ~430 LOC.

### B1. Design

The `with` statement compiles to a try/finally block with explicit enter/exit calls:

```python
with open("file.txt") as f:
    data = f.read()
```

Desugars to:
```python
_mgr = open("file.txt")
f = _mgr.__enter__()
try:
    data = f.read()
except:
    if not _mgr.__exit__(*sys.exc_info()):
        raise
else:
    _mgr.__exit__(None, None, None)
```

MIR codegen:
```
reg_mgr   = <eval context manager expression>
reg_val   = CALL py_context_enter(reg_mgr)       // __enter__
<bind reg_val to as-target if present>
// set up exception-handling frame (same mechanism as try/except)
L_try:
    <compile body>
    CALL py_context_exit(reg_mgr, ItemNull, ItemNull, ItemNull)
    JMP L_end
L_except:
    reg_suppress = CALL py_context_exit(reg_mgr, <exc_type>, <exc_val>, ItemNull)
    IF reg_suppress == false: re-raise exception
L_end:
```

### B2. Runtime Functions

**`py_context_enter(mgr: Item) → Item`**
```c
Item py_context_enter(Item mgr) {
    // look up __enter__ via py_getattr(mgr, "__enter__")
    // call it with no args (self already bound)
    // for built-in file objects (from py_builtin_open): no-op, return mgr
    Item enter = py_getattr(mgr, it_str("__enter__"));
    if (is_null(enter)) return mgr;           // duck-type fallback
    return py_call_function(enter, NULL, 0);
}
```

**`py_context_exit(mgr, exc_type, exc_val, exc_tb) → Item`**
```c
Item py_context_exit(Item mgr, Item exc_type, Item exc_val, Item exc_tb) {
    // look up __exit__ via py_getattr(mgr, "__exit__")
    // call with (exc_type, exc_val, exc_tb)
    // return value: if truthy, suppress the exception
    // for built-in file objects: flush/close the file
    Item exit_fn = py_getattr(mgr, it_str("__exit__"));
    if (is_null(exit_fn)) return b2it(false);
    Item args[3] = { exc_type, exc_val, exc_tb };
    return py_call_function(exit_fn, args, 3);
}
```

**Files:** `py_runtime.cpp` additions (~150 LOC), `py_runtime.h` (2 decls), `sys_func_registry.c` (2 entries).

### B3. Transpiler

The AST already provides `PyWithNode` with `items` (context manager expression), `target` (optional `as` name), and `body`.

**Fix:** The current `build_py_with_statement` in `build_py_ast.cpp` does not extract the `as` target. This needs a small fix to read the `with_item`'s `as` field:

```c
// build_py_ast.cpp — add to build_py_with_statement:
TSNode as_node = ts_node_child_by_field_name(item, "alias", 5);
if (!ts_node_is_null(as_node)) {
    StrView as_src = py_node_source(tp, as_node);
    with->target = name_pool_create_len(tp->name_pool, as_src.str, as_src.length);
}
```

**Transpiler addition** in `pm_compile_statement` for `PY_AST_NODE_WITH`:

```c
case PY_AST_NODE_WITH: {
    PyWithNode* wn = (PyWithNode*)node;
    // 1. eval context manager
    MIR_reg_t reg_mgr = pm_transpile_expr(mt, wn->items);
    // 2. __enter__
    MIR_reg_t reg_val = pm_call1(mt, "py_context_enter", reg_mgr);
    // 3. bind as-target if present
    if (wn->target) pm_set_local(mt, wn->target, reg_val);
    // 4. emit try/finally wrapper
    //    - compile body inside try
    //    - in except: call py_context_exit with exc info
    //    - in finally: call py_context_exit(mgr, NULL, NULL, NULL)
    break;
}
```

**Files:** `build_py_ast.cpp` fix (~30 LOC), `transpile_py_mir.cpp` additions (~200 LOC).

### Test Plan

```python
# test/py/test_py_with.py

# User-defined context manager
class Timer:
    def __enter__(self):
        self.active = True
        return self

    def __exit__(self, exc_type, exc_val, exc_tb):
        self.active = False
        return False   # don't suppress exceptions

with Timer() as t:
    print(t.active)    # True
print(t.active)        # False

# Exception suppression
class Suppress:
    def __init__(self, *exc_types):
        self.exc_types = exc_types

    def __enter__(self): return self

    def __exit__(self, exc_type, exc_val, exc_tb):
        return exc_type is not None

with Suppress(ValueError):
    raise ValueError("ignored")    # suppressed
print("after suppressed with")     # after suppressed with
```

---

## 4. Phase C: Decorators

**Goal:** Enable `@decorator` syntax for functions and classes. Function decorators are syntactic sugar for `f = decorator(f)`, which is straightforward to transpile.

**Estimated effort:** ~280 LOC.

### C1. Function Decorators

```python
@timer
@memoize(maxsize=128)
def fib(n):
    return n if n <= 1 else fib(n-1) + fib(n-2)
```

Desugars to:
```python
def fib(n): ...
fib = memoize(maxsize=128)(fib)
fib = timer(fib)
```

Decorators are applied in bottom-to-top order (innermost decorator applied first).

**AST:** `PyClassDefNode.decorators` is already populated by `build_py_ast.cpp` (line 313 in `py_print.cpp` shows it's handled in the printer). The `PyFunctionDefNode` should have a `decorators` field too — check and add if missing.

**Transpiler:** After compiling the function MIR item (in phase 1.9 / 2), emit decorator applications at the definition point in the codegen order:

```
MIR codegen for:
    @timer
    def foo(): ...

Phase 2 (where the def appears):
    // foo's MIR function was already emitted in phase 1.9
    _py_foo = <ref to pyf_foo MIR item>          // initial binding
    _py_foo = CALL py_call_function(_py_timer, _py_foo)   // apply decorator
```

For decorator factories (`@memoize(maxsize=128)`):
```
    reg_partial = CALL py_call_function(_py_memoize, <128>_py_kw)
    _py_fib = CALL py_call_function(reg_partial, _py_fib_raw)
```

The decorator expression is a full Python expression — any call or attribute access is valid. The transpiler emits it as a regular expression, then wraps the function reference.

### C2. Class Decorators

Same mechanism, applied after `py_class_new`:
```
    _py_MyClass = CALL py_class_new(...)
    _py_MyClass = CALL py_call_function(_py_my_decorator, _py_MyClass)
```

### C3. `@property` (Basic)

The `@property` decorator is special — it creates a descriptor. A minimal implementation:
- `property(fget)` → returns a Map `{ "__is_property__": true, "__get__": fget }`
- `py_getattr` checks for `__is_property__` when walking class dict and calls `__get__` with the instance.

For `@prop.setter` / `@prop.deleter` — deferred to v3.1.

**Files:** `transpile_py_mir.cpp` additions (~200 LOC), `py_builtins.cpp` `property()` builtin (~80 LOC).

### Test Plan

```python
# test/py/test_py_decorators.py
def twice(fn):
    def wrapper(*args):
        fn(*args)
        fn(*args)
    return wrapper

@twice
def greet(name):
    print(f"Hello, {name}")

greet("World")
# Hello, World
# Hello, World

# Class decorator
def add_repr(cls):
    def __repr__(self):
        return f"<{cls.__name__} instance>"
    cls.__repr__ = __repr__
    return cls

@add_repr
class Point:
    def __init__(self, x, y):
        self.x = x
        self.y = y

p = Point(1, 2)
print(repr(p))    # <Point instance>
```

---

## 5. Phase D: `**kwargs`

**Goal:** Complete the function calling convention with `**kwargs` parameter packing and `**dict` argument unpacking.

**Estimated effort:** ~420 LOC.

### D1. Design

The AST already handles `PY_AST_NODE_DICT_SPLAT_PARAMETER` (line 78 of `py_ast.hpp`). The transpiler skips it with a comment `// **kwargs — skip for now`. The call side handles `**dict` splat arguments similarly (partially).

**Function definition with `**kwargs`:**

```python
def f(a, b=10, *args, **kwargs):
    print(a, b, args, kwargs)
```

`**kwargs` collects all keyword arguments not matched to explicit parameters into a Map.

**Calling convention extension:**

The current approach for user functions passes arguments positionally (with defaults padded as `PY_ITEM_NULL_VAL`). With `**kwargs`, we need to pass the overflow keyword arguments as an additional Map argument.

Two options:

**(A) Extra Map parameter:** Functions with `**kwargs` get an extra `i64:_py__kwargs_map` parameter. Call sites pass either an empty Map or a populated one.

**(B) Shared `argc/argv` convention:** When any function uses `**kwargs`, switch to the varargs calling convention (like builtins: `args*` + `argc`). This is a larger refactor.

**Recommendation: Option A.** It fits the existing per-function MIR sig model. For functions without `**kwargs`, no change. For functions with `**kwargs`, add one extra Map parameter.

### D2. Function Entry — `**kwargs` Packing

In `pm_compile_function`, when `fc->has_kwargs` is true:

```c
// PyFuncCollected additions:
bool has_kwargs;
char kwargs_name[128];   // "_py_kwargs" or user's name

// MIR signature: ..., i64:_py__kwargs_map
// at function entry (already done):
//   explicit keyword args are resolved at call sites (A2 from v2)
//   _py__kwargs_map receives any leftover keyword args from the caller
```

### D3. Call Sites — `**dict` Argument Unpacking

At call sites with `**expr`:

```python
opts = {"color": "red", "size": 10}
draw(x, y, **opts)
```

The transpiler collects all `**dict` arguments and merges them into the kwargs Map:

```
reg_kwargs = CALL py_dict_new()
// for each **expr at call site:
CALL py_dict_merge(reg_kwargs, <eval expr>)
// pass reg_kwargs as extra param to function (if it accepts **kwargs)
// for functions without **kwargs: distribute known keys as keyword args, error on unknown
```

**`py_dict_merge(dst, src)`** — copies all entries from `src` into `dst`:

```c
Item py_dict_merge(Item dst, Item src) {
    // iterate src's fields, set each into dst
}
```

### D4. `PyFuncCollected` and `PyMirTranspiler` Additions

```c
// transpile_py_mir.cpp additions to PyFuncCollected:
bool has_kwargs;
char kwargs_name[128];
```

**Files:** `transpile_py_mir.cpp` additions (~300 LOC), `py_runtime.cpp` `py_dict_merge` (~80 LOC), `py_runtime.h` (1 decl), `sys_func_registry.c` (1 entry).

### Test Plan

```python
# test/py/test_py_kwargs.py
def configure(host, port=8080, **options):
    print(f"{host}:{port}")
    for k, v in options.items():
        print(f"  {k}={v}")

configure("localhost", debug=True, timeout=30)
# localhost:8080
#   debug=True
#   timeout=30

configure("example.com", 443, ssl=True)
# example.com:443
#   ssl=True

# ** unpacking at call sites
defaults = {"debug": False, "timeout": 60}
overrides = {"timeout": 30}
configure("server", **defaults, **overrides)
# server:8080
#   debug=False
#   timeout=30
```

---

## 6. Phase E: Single-File Imports

**Goal:** Enable `import module` and `from module import name` for single-file `.py` scripts in the same directory.

**Estimated effort:** ~680 LOC (new `py_module.cpp` + `py_module.h`).

### E1. Design

Python's full import system (packages, `__init__.py`, `sys.path` search, circular dependencies, relative imports) is complex. v3 targets the 80% case for scripting:

| Pattern | v3 Support |
|---------|-----------|
| `import utils` | ✅ — load `utils.py` relative to current file |
| `from utils import helper` | ✅ — load `utils.py`, extract `helper` binding |
| `from utils import *` | ✅ — extract all top-level bindings |
| `import json` / `import math` | ⚠️ No-op (deferred to v4 built-in modules) |
| `import pkg.submod` | ❌ Deferred — no package resolution |
| Relative imports (`from . import x`) | ❌ Deferred |
| Circular imports | ❌ Not handled in v3 |

### E2. Module Cache

**New file: `lambda/py/py_module.cpp`** (~450 LOC) + **`lambda/py/py_module.h`** (~60 LOC):

```c
// lambda/py/py_module.h
typedef struct PyModule {
    const char* name;        // module name (interned)
    const char* file_path;   // absolute path to .py file
    Item exports;            // Map of exported names → Item values
    bool loaded;
} PyModule;

// Module registry (global, lives in EvalContext or static)
PyModule* py_import_module(EvalContext* ctx, const char* name, const char* from_file);
Item      py_module_get(PyModule* mod, const char* name);
```

**`py_import_module`** operation:
1. Check module cache (HashMap keyed by absolute file path).
2. If cached, return it.
3. Resolve the module path: try `<dir of current file>/<name>.py`.
4. Compile and execute the module file via `transpile_py_to_mir()`.
5. After execution, collect all module-level variable bindings as the exports Map.
6. Cache and return.

The exports Map is built from the module's `global_var_names` table after execution — a read of all `py_get_module_var(i)` values.

### E3. Transpiler — Import Statement Codegen

**`import utils` →** Load module, bind to `_py_utils` as a Map of its exports:

```
reg_mod = CALL py_import_module("utils", <current_file_path>)
_py_utils = reg_mod
```

Accesses like `utils.helper` then go through `py_getattr(_py_utils, "helper")`.

**`from utils import helper, process` →** Load module, extract specific names:

```
reg_mod  = CALL py_import_module("utils", <current_file_path>)
_py_helper  = CALL py_module_get(reg_mod, "helper")
_py_process = CALL py_module_get(reg_mod, "process")
```

**`from utils import *` →** Copy all export entries into the current scope's module vars.

**`import json` (stdlib stub) →** Currently no-op. Emit a warning to `log.txt` and assign `ItemNull`.

**Files:** `py_module.cpp` (~450 LOC), `py_module.h` (~60 LOC), `transpile_py_mir.cpp` additions (~150 LOC), `sys_func_registry.c` (2 entries), `build_lambda_config.json` (add `py_module.cpp`).

### Test Plan

```python
# test/py/utils.py
def add(a, b):
    return a + b

def multiply(a, b):
    return a * b

PI = 3.14159
```

```python
# test/py/test_py_import.py
from utils import add, multiply, PI

print(add(2, 3))          # 5
print(multiply(4, 5))     # 20
print(PI)                 # 3.14159

import utils
print(utils.add(10, 20))  # 30
```

---

## 7. Implementation Order & Dependencies

```
A1–A3: py_class.cpp/h, runtime updates (py_getattr, py_call_function)
    │
    ├─→ A4: Transpiler CLASS_DEF codegen
    │       │
    │       └─→ A5: super() support
    │
    ├─→ A6: Dunder dispatch in operators
    │
    ├─→ A7: isinstance/issubclass v3
    │
    └─→ A8: Built-in 'object' + exception classes
            │
            ├─→ B: with statement (needs __enter__/__exit__ from A6)
            │
            └─→ C: Decorators (needs class system for class decorators)

D: **kwargs — independent of A/B/C, can do in parallel

E: Import system — independent, but best after A so imported modules can define classes
```

Recommended sequence:
1. **A1–A3** (runtime) + **D** (kwargs) — can be done in parallel as separate PRs
2. **A4** (transpiler class codegen)
3. **A5** (super) + **A6** (dunders) + **A7** (isinstance)
4. **A8** (built-in classes) + **B** (with statement)
5. **C** (decorators)
6. **E** (imports — last, as it exercises the full class + module pipeline)

---

## 8. LOC Estimate Summary

| Phase | File(s) | New LOC | Notes |
|-------|---------|---------|-------|
| A1–A3 | `py_class.cpp` (new) | ~700 | class_new, compute_mro, new_instance, super, isinstance, bound method |
| A1 | `py_class.h` (new) | ~80 | C API declarations |
| A2–A3 | `py_runtime.cpp` | +350 | py_getattr/setattr class support, py_call_function bound method + class call |
| A4–A5 | `transpile_py_mir.cpp` | +700 | CLASS_DEF collection + codegen, super() transform, call-site class detect |
| A6 | `py_runtime.cpp` | +150 | Dunder dispatch in 12 operator functions |
| A7 | `py_class.cpp` + `py_builtins.cpp` | +100 | Full MRO-based isinstance/issubclass |
| A8 | `py_class.cpp` | +80 | Built-in class hierarchy init |
| B | `transpile_py_mir.cpp` + `py_runtime.cpp` + `build_py_ast.cpp` | +430 | with codegen, context_enter/exit, as-target fix |
| C | `transpile_py_mir.cpp` + `py_builtins.cpp` | +280 | Decorator application, property() builtin |
| D | `transpile_py_mir.cpp` + `py_runtime.cpp` | +420 | **kwargs packing, ** call-site unpacking, py_dict_merge |
| E | `py_module.cpp` (new) + `py_module.h` (new) + `transpile_py_mir.cpp` | +680 | Module cache, import codegen |
| Tests | `test/py/*.py` + `test/py/*.txt` | ~300 | classes, inheritance, with, decorators, kwargs, import |
| **Total** | | **~4,270** | |

**v3 total projection:** ~10,865 (v2) + ~4,270 (v3 new) = **~15,135 LOC**

---

## 9. Testing Strategy

### Regression
All existing test scripts in `test/py/` must continue to pass after each phase.

### New Test Files

| Test file | Phase | Key coverage |
|-----------|-------|-------------|
| `test_py_classes.py` | A | Single inheritance, `__init__`, methods, class attrs, `__str__` |
| `test_py_multiple_inherit.py` | A | Multiple inheritance, C3 MRO, diamond pattern |
| `test_py_dunders.py` | A6 | `__add__`, `__eq__`, `__iter__`, `__getitem__`, `__len__`, `__bool__` |
| `test_py_super.py` | A5 | `super()` in single + multiple inheritance |
| `test_py_isinstance.py` | A7 | `isinstance` + `issubclass` with real class hierarchy |
| `test_py_with.py` | B | User-defined context managers, exception suppression |
| `test_py_with_file.py` | B | `with open(...)` (uses `py_context_exit` to close) |
| `test_py_decorators.py` | C | Function and class decorators |
| `test_py_property.py` | C3 | `@property` getter |
| `test_py_kwargs.py` | D | `**kwargs` in definitions, `**dict` at call sites |
| `test_py_import.py` | E | `from utils import x`, `import utils`, `utils.x` |

### Integration Test

```python
# test/py/test_py_integration_v3.py
# Exercises: classes, inheritance, with, decorators, kwargs, imports

from utils import add   # Phase E

class Vector:
    def __init__(self, x, y):
        self.x = x
        self.y = y

    def __add__(self, other):
        return Vector(self.x + other.x, self.y + other.y)

    def __repr__(self):
        return f"Vector({self.x}, {self.y})"

    def __len__(self):
        return 2

def logged(fn):
    def wrapper(*args, **kwargs):
        result = fn(*args, **kwargs)
        print(f"  → {result}")
        return result
    return wrapper

@logged
def make_vector(x, y=0):
    return Vector(x, y)

v1 = make_vector(1, 2)    # → Vector(1, 2)
v2 = make_vector(3, y=4)  # → Vector(3, 4)
v3 = v1 + v2
print(v3)                 # Vector(4, 6)

print(isinstance(v1, Vector))  # True
print(len(v1))                 # 2

# with: ensure resource cleanup
class Resource:
    def __init__(self, name): self.name = name
    def __enter__(self): print(f"open {self.name}"); return self
    def __exit__(self, *_): print(f"close {self.name}"); return False

with Resource("db") as r:
    print(f"using {r.name}")
# open db
# using db
# close db

print(f"add(10, 20) = {add(10, 20)}")   # add(10, 20) = 30
print("all tests passed")
```

---

## Appendix: What's Deferred to v4+

| Feature | Reason | Proposed Version |
|---------|--------|-----------------|
| `async`/`await`/`yield`/generators | Requires coroutine/generator runtime with suspend/resume | v4 |
| `match`/`case` (Python 3.10+) | Complex pattern-matching AST; niche for scripting use cases | v4 |
| Arbitrary-precision `int` | 64-bit sufficient for most programs | v4 |
| `bytes`/`bytearray` | Binary data type | v4 |
| Metaclasses | `__init_subclass__`, `__class_getitem__`, custom metaclass | v4 |
| Descriptor protocol (full) | `__get__`/`__set__`/`__delete__` beyond basic `@property` | v4 |
| Package imports (`import pkg.sub`) | Needs `__init__.py` traversal, `sys.path` | v4 |
| Circular import detection | Needs import-stack tracking | v4 |
| `@prop.setter` / `@prop.deleter` | Needs `property` object to be mutable | v3.1 |
| `__slots__` optimization | Needs class system first (now available), but adds complexity | v3.1 |
| Nested comprehensions | `[[i*j for j in row] for row in matrix]` — scope stack in comprehension emit | v3.1 |
| `__init_subclass__` | Class initialization hook | v4 |
| Standard library modules (`json`, `math`, `re`) | Separate module bridge layer | v4 |
