# Python Support in Lambda

Lambda runs Python scripts through the hosted-language module `lang-python`:

```bash
./lambda.exe py script.py                     # the language alias
./lambda.exe script.py                        # dispatch on the .py extension
./lambda.exe run --lang python script.py      # the explicit form
```

> **Status (alpha).** Python is an external Jube module, not part of the host binary: a dev build gets it with `make build-lang-python` (see [Lambda_Jube_Runtime.md](dev/Lambda_Jube_Runtime.md)), and the full release bundle ships it beside the executable. Without the module every form above prints a hosted-language-unavailable diagnostic. The module passes 39 of its 43 test scripts; module import and package resolution are currently unstable (their tests crash), so treat multi-file Python programs as experimental until that regression is fixed.

Python source is parsed with tree-sitter-python, built into the shared unified AST, and run by the Lambda runtime — interpreted, or compiled to native code through the MIR JIT. All Python values are Lambda `Item` values, so there is no conversion boundary between the two runtimes. Python is a **dialect hosted on Lambda's substrate**: what the tables below do not list is not supported.

---

## Supported Features

### Data Types

| Type | Status | Notes |
|------|--------|-------|
| `int` | **Full** | Small ints inline; arbitrary precision on overflow |
| `float` | **Full** | 64-bit IEEE 754 double |
| `bool` | **Full** | `True`/`False` |
| `None` | **Full** | |
| `str` | **Full** | Interned via name pool; escape sequences supported |
| `list` | **Full** | Lambda `Array`, dynamic growth |
| `tuple` | **Full** | Stored as `Array` (immutable semantics not enforced) |
| `dict` | **Full** | Lambda `Map` with `ShapeEntry` chain |
| `set` | **Partial** | Deduplicating set with membership; the set algebra operators (`\|`, `&`, `-`) are not verified |
| `bytes` / `bytearray` | **Not supported** | |
| `complex` | **Not supported** | |
| `frozenset` | **Not supported** | |

### Literals

| Literal | Status | Notes |
|---------|--------|-------|
| Integers (decimal) | **Full** | `42`, `-7` |
| Integers (hex/oct/bin) | **Full** | `0xff`, `0o77`, `0b101` |
| Floats | **Full** | `3.14`, `1e-5` |
| Strings | **Full** | Single/double quotes, escape sequences (`\n`, `\t`, `\\`, etc.) |
| Triple-quoted strings | **Parsed** | Newlines preserved |
| Raw strings (`r"..."`) | **Partial** | Parsed but raw escape handling may be incomplete |
| Byte strings (`b"..."`) | **Not supported** | |
| F-strings | **Partial** | Basic interpolation `f"{x}"` and format specs `f"{x:.2f}"` work; `f"{x=}"` debug format not supported |
| `True` / `False` / `None` | **Full** | |

### Operators

| Category | Operators | Status |
|----------|-----------|--------|
| Arithmetic | `+`, `-`, `*`, `/`, `//`, `%`, `**` | **Full** — Python semantics (floor div rounds toward −∞, modulo has sign of divisor) |
| Unary | `-x`, `+x`, `~x` | **Full** |
| Bitwise | `&`, `\|`, `^`, `<<`, `>>` | **Full** |
| Comparison | `==`, `!=`, `<`, `<=`, `>`, `>=` | **Full** |
| Identity | `is`, `is not` | **Full** |
| Membership | `in`, `not in` | **Full** — works on lists, strings, dicts |
| Boolean | `and`, `or`, `not` | **Full** — value-returning with short-circuit |
| Chained comparison | `a < b < c` | **Full** — proper short-circuit evaluation |
| Ternary | `x if cond else y` | **Full** |
| Augmented assignment | `+=`, `-=`, `*=`, `/=`, `//=`, `%=`, `**=`, `&=`, `\|=`, `^=`, `<<=`, `>>=` | **Full** — variables and subscripts |
| Matmul | `@`, `@=` | **Not supported** — operator parsed but no-op |
| Walrus | `:=` | **Parsed only** — not evaluated |
| String `%` formatting | `"hello %s" % name` | **Full** |

### Numeric Semantics

| Behavior | Status |
|----------|--------|
| True division `/` always returns float | **Full** |
| Floor division `//` rounds toward −∞ | **Full** |
| Modulo `%` result has sign of divisor | **Full** |
| Negative exponents return float | **Full** |
| Integer overflow → arbitrary-precision int | **Full** |
| String repetition `"ab" * 3` | **Full** |
| List repetition `[1] * 3` | **Full** |
| List concatenation `[1] + [2]` | **Full** |
| String concatenation `"a" + "b"` | **Full** |

### Variables & Assignment

| Feature | Status | Notes |
|---------|--------|-------|
| Simple assignment `x = 5` | **Full** | |
| Multiple targets `a = b = 5` | **Full** | Walks targets list |
| Tuple unpacking `a, b = 1, 2` | **Full** | |
| Subscript assignment `a[0] = 5` | **Full** | |
| Attribute assignment `obj.x = 5` | **Full** | |
| Augmented subscript `a[0] += 1` | **Full** | |
| Starred unpacking `a, *rest = [1,2,3]` | **Not supported** | Parsed but not transpiled |
| Slice assignment `a[1:3] = [4,5]` | **Full** | |

### Control Flow

| Feature | Status | Notes |
|---------|--------|-------|
| `if` / `elif` / `else` | **Full** | Arbitrary nesting |
| `while` loop | **Full** | With `break` / `continue` |
| `for` loop | **Full** | Index-based iteration over lists/ranges |
| `for` with tuple unpacking | **Full** | `for a, b in pairs:` |
| `break` / `continue` | **Full** | |
| `pass` | **Full** | |
| `for...else` | **Not supported** | |
| `while...else` | **Not supported** | |
| `match` / `case` (3.10+) | **Full** | Literal, capture, sequence, mapping and class patterns |

### Functions

| Feature | Status | Notes |
|---------|--------|-------|
| `def` with positional params | **Full** | Up to 16 parameters |
| `return` with value | **Full** | Implicit `return None` at end |
| Nested function definitions | **Full** | |
| Forward references | **Full** | Functions collected in pre-pass (callable before definition) |
| Default parameter values | **Full** | |
| Keyword arguments in calls | **Full** | Named and mixed positional/keyword |
| `*args` parameter | **Full** | Collects extra positional args as list |
| `**kwargs` parameter | **Full** | Collects extra keyword args as dict |
| `*args` unpacking in calls | **Full** | `f(*lst)` spreads list as positional args |
| `**kwargs` unpacking in calls | **Full** | `f(**dct)` spreads dict as keyword args |
| Closures (variable capture) | **Full** | Multi-level capture across nested scopes |
| `lambda` expressions | **Full** | |
| Generators (`yield`, `yield from`) | **Full** | Generator functions and generator expressions |
| `async def` / `await` | **Full** | Coroutines run on the shared event loop |
| Recursion | **Full** | |

### Built-in Functions (40+ implemented)

| Function | Status | Notes |
|----------|--------|-------|
| `print(...)` | **Full** | `*args`, `sep=` and `end=` |
| `len(x)` | **Full** | Strings, lists, dicts, class instances (`__len__`) |
| `type(x)` | **Full** | Returns `<class 'int'>` etc. |
| `isinstance(x, t)` | **Full** | Full MRO-based check; handles built-in and user-defined classes |
| `issubclass(a, b)` | **Full** | Full MRO-based check |
| `int(x)` | **Full** | Numeric/string conversion |
| `float(x)` | **Full** | Numeric/string conversion |
| `str(x)` | **Full** | Converts any value; calls `__str__` on class instances |
| `bool(x)` | **Full** | Python truthiness rules; calls `__bool__` on class instances |
| `repr(x)` | **Full** | Calls `__repr__` on class instances |
| `abs(x)` | **Full** | Int and float |
| `round(x, n=0)` | **Full** | |
| `range(...)` | **Full** | 1/2/3 args, negative step supported |
| `min(...)` | **Full** | Variadic |
| `max(...)` | **Full** | Variadic |
| `sum(iterable)` | **Full** | |
| `all(iterable)` | **Full** | |
| `any(iterable)` | **Full** | |
| `sorted(iterable)` | **Full** | `key=` and `reverse=` arguments supported |
| `reversed(iterable)` | **Full** | Returns new list |
| `enumerate(iterable)` | **Full** | Returns list of `(i, val)` tuples |
| `zip(...)` | **Full** | |
| `map(f, iterable)` | **Full** | |
| `filter(f, iterable)` | **Full** | |
| `iter(x)` | **Full** | Calls `__iter__`; native iterables and class instances |
| `next(x)` | **Full** | Calls `__next__`; raises `StopIteration` at end |
| `ord(c)` | **Full** | Character → int |
| `chr(n)` | **Full** | Int → character |
| `input(prompt)` | **Full** | Reads from stdin |
| `hash(x)` | **Full** | |
| `id(x)` | **Full** | |
| `open(file, mode)` | **Partial** | Text mode read/write; binary mode limited |
| `list(iterable)` | **Full** | |
| `tuple(iterable)` | **Full** | |
| `dict(...)` | **Partial** | `dict()` and `dict(**kwargs)` forms; `dict(pairs)` constructor partial |
| `set(iterable)` | **Partial** | Deduplicates; set operators not verified |
| `getattr(obj, name)` | **Full** | |
| `setattr(obj, name, val)` | **Full** | |
| `hasattr(obj, name)` | **Full** | |
| `property(fget)` | **Full** | With `@prop.setter` |
| `staticmethod(f)` | **Full** | Via `@staticmethod` decorator |
| `super()` | **Full** | Zero-argument form inside methods |

Also implemented: `bin`, `oct`, `hex`, `divmod`, `pow` (3-arg), `callable`.

**Not implemented:** `compile`, `eval`, `exec`, `dir`, `vars`, `delattr`, `globals`, `locals`, `ascii`, `bytes`, `bytearray`, `complex`, `frozenset`, `format`, `classmethod`, `slice`.

### String Methods

| Method | Status |
|--------|--------|
| `upper()` | **Full** |
| `lower()` | **Full** |
| `strip()` | **Full** |
| `lstrip()` | **Full** |
| `rstrip()` | **Full** |
| `split(sep=None)` | **Full** — whitespace or explicit separator |
| `join(iterable)` | **Full** |
| `replace(old, new)` | **Full** |
| `find(sub)` | **Full** — returns -1 on miss |
| `startswith(prefix)` | **Full** |
| `endswith(suffix)` | **Full** |
| `count(sub)` | **Full** |
| `isdigit()` | **Full** |
| `isalpha()` | **Full** |
| `title()` | **Full** |
| `capitalize()` | **Full** |
| `format(...)` | **Full** — positional and named fields, format specs |
| `center`, `ljust`, `rjust`, `zfill` | **Full** |
| `index`, `rfind` | **Full** |
| `isalnum`, `islower`, `isupper`, `isspace` | **Full** |
| `splitlines`, `swapcase` | **Full** |

**Not implemented:** `casefold`, `encode`, `expandtabs`, `isdecimal`, `isnumeric`, `isprintable`, `istitle`, `maketrans`, `partition`, `rindex`, `rpartition`, `rsplit`, `translate`.

### List Methods (11 implemented)

| Method | Status |
|--------|--------|
| `append(x)` | **Full** |
| `extend(iterable)` | **Full** |
| `insert(i, x)` | **Full** |
| `pop(i=-1)` | **Full** |
| `remove(x)` | **Full** |
| `index(x)` | **Full** |
| `count(x)` | **Full** |
| `sort()` | **Full** | `key=` and `reverse=` arguments supported |
| `reverse()` | **Full** |
| `copy()` | **Full** |
| `clear()` | **Full** |

### Dict Methods

| Method | Status | Notes |
|--------|--------|-------|
| `keys()` | **Full** | Returns list |
| `values()` | **Full** | Returns list |
| `items()` | **Full** | Returns list of tuples |
| `get(key, default=None)` | **Full** | |
| `update(other)` | **Full** | |
| `pop(key)` | **Partial** | Returns value but may not remove from shape |
| `clear()` | **Full** | |
| `copy()`, `popitem()`, `setdefault(key, default)` | **Full** | |

**Not implemented:** `fromkeys`.

### Truthiness

Follows Python's truthiness rules exactly:

| Value | Result |
|-------|--------|
| `None` | `False` |
| `False` | `False` |
| `0`, `0.0` | `False` |
| `""` | `False` |
| `[]` | `False` |
| `{}` | `False` |
| Everything else | `True` |

### Scoping

| Feature | Status | Notes |
|---------|--------|-------|
| Local scope | **Full** | Assignment creates in current scope |
| Module (global) scope | **Full** | Top-level variables |
| Variable lookup (inner → outer) | **Full** | LEGB chain walk |
| `global` declaration | **Full** | Rebinds the module-level name |
| `nonlocal` declaration | **Full** | Rebinds the enclosing function's name |

### Assertions

| Feature | Status |
|---------|--------|
| `assert condition` | **Full** — raises AssertionError on failure |
| `assert condition, message` | **Full** |

### Subscripting & Indexing

| Feature | Status |
|---------|--------|
| List indexing `a[0]` | **Full** |
| Negative indexing `a[-1]` | **Full** |
| Dict key access `d["key"]` | **Full** |
| String indexing `s[0]` | **Full** |
| Subscript assignment `a[0] = x` | **Full** |
| Dict key assignment `d["k"] = v` | **Full** |
| Slicing `a[1:3]` | **Full** |
| Slice with step `a[::2]` | **Full** |
| Slice assignment `a[1:3] = [4,5]` | **Full** |

### Comprehensions

| Feature | Status | Notes |
|---------|--------|-------|
| List comprehension `[x*2 for x in lst]` | **Full** | Including `if` filter clause |
| Dict comprehension `{k: v for k, v in items}` | **Full** | |
| Set comprehension `{x*2 for x in lst}` | **Full** | |
| Generator expressions `(x*2 for x in lst)` | **Full** | Lazy; consumed by `for`, `list()`, `sum()`, … |

### Classes

| Feature | Status | Notes |
|---------|--------|-------|
| `class` definitions | **Full** | |
| Instance creation `ClassName(args)` | **Full** | Calls `__init__` |
| Single inheritance | **Full** | |
| Multiple inheritance | **Full** | C3 linearization (MRO) |
| `super()` | **Full** | Zero-argument form; walks MRO from containing class |
| Instance methods | **Full** | First parameter (`self`) implicitly passed |
| Class attributes | **Full** | Accessible via `ClassName.attr` and instance lookup |
| `@staticmethod` | **Full** | No `self` / `cls` parameter |
| `@classmethod` | **Not supported** | |
| `@property` | **Full** | Getter and `@prop.setter` |
| Descriptors (`__get__`, `__set__`) | **Full** | |
| `isinstance(x, cls)` | **Full** | Full MRO-based check |
| `issubclass(a, b)` | **Full** | Full MRO-based check |
| `__init__`, `__str__`, `__repr__` | **Full** | |
| `__len__`, `__bool__`, `__eq__` | **Full** | |
| `__add__`, `__sub__`, `__mul__`, `__truediv__`, `__floordiv__`, `__mod__` | **Full** | Including `__radd__` etc. |
| `__lt__`, `__le__`, `__gt__`, `__ge__` | **Full** | |
| `__iter__`, `__next__` | **Full** | Custom iterators |
| `__getitem__`, `__setitem__` | **Full** | |
| `__contains__` | **Full** | `in` operator |
| `__enter__`, `__exit__` | **Full** | Context manager protocol |
| Metaclasses | **Not supported** | |
| `__slots__` | **Not supported** | |

### Exception Handling

| Feature | Status | Notes |
|---------|--------|-------|
| `raise ExceptionType(msg)` | **Full** | |
| `raise` (re-raise current exception) | **Full** | |
| `try` / `except` | **Full** | Multiple `except` clauses |
| `except ExceptionType as e` | **Full** | Binds caught exception to name |
| `except (TypeA, TypeB)` | **Full** | Tuple of exception types |
| `finally` | **Full** | Always executes |
| `try...else` clause | **Full** | |
| Exception chaining (`raise X from Y`) | **Not supported** | |
| Custom exception classes | **Full** | `class MyError(Exception): ...` |
| Built-in exception types | **Full** | `ValueError`, `TypeError`, `KeyError`, `IndexError`, `RuntimeError`, `StopIteration`, `AssertionError`, `AttributeError`, `NotImplementedError`, `Exception` |

### Decorators

| Feature | Status | Notes |
|---------|--------|-------|
| Function decorators `@dec` | **Full** | |
| Class decorators `@dec` | **Full** | |
| Method decorators | **Full** | |
| Decorator factories `@dec(args)` | **Full** | |
| Stacked decorators | **Full** | Applied bottom-to-top |
| `@property` and `@prop.setter` | **Full** | |
| `@staticmethod` | **Full** | |
| `@classmethod` | **Not supported** | |
| `@prop.deleter` | **Not supported** | |

### `with` Statement

| Feature | Status | Notes |
|---------|--------|-------|
| `with expr as var:` | **Full** | Calls `__enter__`; binds result to `var` |
| `with expr:` (no `as` target) | **Full** | |
| Exception suppression via `__exit__` | **Full** | Return `True` from `__exit__` to suppress |
| Nested `with` | **Full** | |
| `contextlib.contextmanager` | **Not supported** | Generator-based context managers |

### Imports

| Pattern | Status | Notes |
|---------|--------|-------|
| `import module` | **Full** | Loads `.py` relative to the importing script; namespace accessible via `module.name` |
| `from module import name` | **Full** | |
| `from module import name as alias` | **Full** | |
| `from module import *` | **Full** | All non-dunder top-level names |
| Standard library | **Partial** | Native shims for `json`, `time`, `random`, `functools`, `collections`, `math`, `os`, `sys`, `re`, `copy`, `enum`, `abc`, `array`, `io` |
| `import pkg.submodule` | **Full** | Package and `__init__.py` resolution — currently unstable (see the status note) |
| `from . import x` (relative imports) | **Not supported** | |
| Circular imports | **Not supported** | |

---

## Not Supported

### Delete

`del` — parsed, currently a no-op.

### Remaining Assignment Forms

| Feature | Status |
|---------|--------|
| Starred unpacking `a, *rest = lst` | **Not supported** |
| Slice assignment `a[1:3] = [4,5]` | **Not supported** |
| List spread `[*a, *b]` | **Not supported** |
| Dict spread `{**d1, **d2}` literal | **Not supported** — `**` unpacking at call sites is supported |

### Advanced String Features

| Feature | Status |
|---------|--------|
| F-string `=` debug format `f"{x=}"` | **Not supported** |

### Remaining Class / Decorator Gaps

| Feature | Status |
|---------|--------|
| `@classmethod` | **Not supported** |
| `@prop.deleter` | **Not supported** |
| Metaclasses | **Not supported** |
| `__init_subclass__`, `__class_getitem__` | **Not supported** |
| `__slots__` | **Not supported** |

### Import Limitations

| Pattern | Status |
|---------|--------|
| Standard library beyond the shimmed modules | **Not supported** |
| `from . import x` (relative imports) | **Not supported** |
| Circular imports | **Not supported** |
| Module import / packages | **Unstable** — implemented, but the import and package tests currently crash |

### Other Limitations

| Feature | Status |
|---------|--------|
| `for...else` / `while...else` | **Not supported** |
| Exception chaining `raise X from Y` | **Not supported** |
| `contextlib.contextmanager` | **Not supported** |
| Positional-only params `/` | **Not supported** |
| Keyword-only params `*` separator | **Not supported** |
| `:=` walrus operator | **Parsed only** |
| `@` matmul operator | **Not supported** |
| Type annotations | **Parsed and ignored** |
| `__name__` / `__doc__` introspection | **Not supported** |

---

## Summary

| Category | Supported | Partial | Not Supported |
|----------|-----------|---------|---------------|
| **Literals & types** | int (arbitrary precision), float, str, bool, None, list, tuple, dict, set | f-strings (no `=` debug) | bytes, complex, frozenset |
| **Operators** | All arithmetic, bitwise, comparison, boolean, chained, `%` formatting | | `@` matmul, `:=` walrus (parsed only) |
| **Control flow** | if/elif/else, for, while, break/continue, pass, match/case | | for-else, while-else |
| **Functions** | def, return, nested, recursion, forward refs, defaults, *args, **kwargs, closures, lambda, generators, async | | — |
| **Builtins** | 45+ functions incl. print(sep=, end=), bin/oct/hex, divmod, callable | open (text only) | eval/exec, classmethod, slice |
| **String methods** | 30 working incl. format | | casefold, partition, rsplit, translate, … |
| **List methods** | 11 working (sort supports key=/reverse=) | | — |
| **Dict methods** | 10 working | pop (partial) | fromkeys |
| **Indexing & slicing** | list/dict/string, negative, slices, step, slice assignment | | — |
| **Comprehensions** | list, dict, set, generator expressions | | — |
| **Classes** | single/multiple inheritance, MRO, dunders, super, @property with setter, @staticmethod, descriptors | | @classmethod, metaclasses, __slots__ |
| **Exceptions** | try/except/else/finally, raise, custom exceptions, 10 built-in types | | exception chaining |
| **Decorators** | function/class/method, factories, stacked, @property, @prop.setter, @staticmethod | | @classmethod, @prop.deleter |
| **`with` statement** | __enter__/__exit__, as-target, exception suppression, nested | | contextlib.contextmanager |
| **Imports** | import mod, from mod import name/alias/*, .py files, 14 stdlib shims, packages | packages (unstable) | other stdlib, relative, circular |
| **Async** | async def / await | | async with, async for (unverified) |
