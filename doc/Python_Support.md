# Python Support in Lambda

Lambda can run Python scripts via the `py` command:

```bash
./lambda.exe py script.py
```

Python source is compiled to native machine code through Tree-sitter parsing, AST construction, and MIR JIT compilation. All Python values are represented as Lambda `Item` values — there is no conversion boundary between the two runtimes.

This document lists every Python language feature and its support status.

---

## Supported Features

### Data Types

| Type | Status | Notes |
|------|--------|-------|
| `int` | **Full** | 56-bit inline; overflows to 64-bit heap |
| `float` | **Full** | 64-bit IEEE 754 double |
| `bool` | **Full** | `True`/`False` |
| `None` | **Full** | |
| `str` | **Full** | Interned via name pool; escape sequences supported |
| `list` | **Full** | Lambda `Array`, dynamic growth |
| `tuple` | **Full** | Stored as `Array` (immutable semantics not enforced) |
| `dict` | **Full** | Lambda `Map` with `ShapeEntry` chain |
| `set` | **Partial** | Stored as list — no deduplication or set operations |
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
| Walrus | `:=` | **Not supported** |
| String `%` formatting | `"hello %s" % name` | **Stub** — returns left operand unchanged |

### Numeric Semantics

| Behavior | Status |
|----------|--------|
| True division `/` always returns float | **Full** |
| Floor division `//` rounds toward −∞ | **Full** |
| Modulo `%` result has sign of divisor | **Full** |
| Negative exponents return float | **Full** |
| Integer overflow → float promotion | **Full** |
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
| Slice assignment `a[1:3] = [4,5]` | **Not supported** | |

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
| `match` / `case` (3.10+) | **Not supported** | |

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
| `lambda` expressions | **Not supported** | |
| Recursion | **Full** | |

### Built-in Functions (40+ implemented)

| Function | Status | Notes |
|----------|--------|-------|
| `print(...)` | **Partial** | `*args` and f-strings supported; `sep=`/`end=` not supported |
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
| `set(iterable)` | **Partial** | No deduplication; set operations not supported |
| `getattr(obj, name)` | **Full** | |
| `setattr(obj, name, val)` | **Full** | |
| `hasattr(obj, name)` | **Full** | |
| `property(fget)` | **Full** | Getter-only; `@prop.setter` not supported |
| `staticmethod(f)` | **Full** | Via `@staticmethod` decorator |
| `super()` | **Full** | Zero-argument form inside methods |

**Not implemented:** `bin`, `oct`, `hex`, `divmod`, `pow` (3-arg), `callable`, `compile`, `eval`, `exec`, `dir`, `vars`, `delattr`, `globals`, `locals`, `ascii`, `bytes`, `bytearray`, `complex`, `frozenset`, `format`, `classmethod`, `slice`.

### String Methods (17 implemented)

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
| `format(...)` | **Stub** — returns string unchanged |

**Not implemented:** `casefold`, `center`, `encode`, `expandtabs`, `index`, `isalnum`, `isdecimal`, `islower`, `isnumeric`, `isprintable`, `isspace`, `istitle`, `isupper`, `ljust`, `maketrans`, `partition`, `rfind`, `rindex`, `rjust`, `rpartition`, `rsplit`, `splitlines`, `swapcase`, `translate`, `zfill`.

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

### Dict Methods (7 implemented)

| Method | Status | Notes |
|--------|--------|-------|
| `keys()` | **Full** | Returns list |
| `values()` | **Full** | Returns list |
| `items()` | **Full** | Returns list of tuples |
| `get(key, default=None)` | **Full** | |
| `update(other)` | **Full** | |
| `pop(key)` | **Partial** | Returns value but may not remove from shape |
| `clear()` | **Full** | |

**Not implemented:** `copy`, `fromkeys`, `popitem`, `setdefault`.

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
| `global` declaration | **Parsed** | Scope analysis only — no runtime effect |
| `nonlocal` declaration | **Parsed** | Scope analysis only — no runtime effect |

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
| Slice assignment `a[1:3] = [4,5]` | **Not supported** |

### Comprehensions

| Feature | Status | Notes |
|---------|--------|-------|
| List comprehension `[x*2 for x in lst]` | **Full** | Including `if` filter clause |
| Dict comprehension `{k: v for k, v in items}` | **Full** | |
| Set comprehension `{x*2 for x in lst}` | **Full** | |
| Generator expressions `(x*2 for x in lst)` | **Not supported** | |

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
| `@property` (getter) | **Full** | Read-only; `@prop.setter` not supported |
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
| `try...else` clause | **Not supported** | |
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
| `@property` (getter) | **Full** | |
| `@staticmethod` | **Full** | |
| `@classmethod` | **Not supported** | |
| `@prop.setter` / `@prop.deleter` | **Not supported** | |

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
| `import json` / stdlib modules | **Not supported** | No stdlib shims in v3 |
| `import pkg.submodule` | **Not supported** | No package/`__init__.py` resolution |
| `from . import x` (relative imports) | **Not supported** | |
| Circular imports | **Not supported** | |

---

## Not Supported

### Lambda Expressions

`lambda x: x*2` — parsed but not transpiled.

### Generators and `yield`

- Generator expressions `(x*2 for x in lst)` — not supported
- `yield` / `yield from` — not supported
- Generator functions — not supported

### Async / Concurrency

- `async def` / `await`
- `async with` / `async for`

### Pattern Matching

`match` / `case` (Python 3.10+) — not supported.

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
| `str.format()` with substitution | **Stub** — returns string unchanged |
| `%` formatting `"hello %s" % name` | **Stub** — returns left string unchanged |

### Remaining Class / Decorator Gaps

| Feature | Status |
|---------|--------|
| `@classmethod` | **Not supported** |
| `@prop.setter` / `@prop.deleter` | **Not supported** |
| Metaclasses | **Not supported** |
| `__init_subclass__`, `__class_getitem__` | **Not supported** |
| `__slots__` | **Not supported** |
| Descriptors beyond `@property` | **Not supported** |

### Import Limitations

| Pattern | Status |
|---------|--------|
| `import json` / stdlib modules | **Not supported** — no stdlib shims in v3 |
| `import pkg.submodule` | **Not supported** — no package/`__init__.py` resolution |
| `from . import x` (relative imports) | **Not supported** |
| Circular imports | **Not supported** |

### Other Limitations

| Feature | Status |
|---------|--------|
| `for...else` / `while...else` | **Not supported** |
| `try...else` clause | **Not supported** |
| Exception chaining `raise X from Y` | **Not supported** |
| `global` / `nonlocal` runtime enforcement | **Parsed only** — no runtime effect |
| `contextlib.contextmanager` | **Not supported** |
| Positional-only params `/` | **Not supported** |
| Keyword-only params `*` separator | **Not supported** |
| `:=` walrus operator | **Not supported** |
| `@` matmul operator | **Not supported** |
| Type annotations | **Parsed and ignored** |
| `__name__` / `__doc__` introspection | **Not supported** |

---

## Summary

| Category | Supported | Partial | Not Supported |
|----------|-----------|---------|---------------|
| **Literals & types** | int, float, str, bool, None, list, tuple, dict | set (as list), f-strings (no `=` debug) | bytes, complex, frozenset |
| **Operators** | All arithmetic, bitwise, comparison, boolean, chained | `%` formatting (stub) | `@` matmul, `:=` walrus |
| **Control flow** | if/elif/else, for, while, break/continue, pass | | for-else, while-else, match/case |
| **Functions** | def, return, nested, recursion, forward refs, defaults, *args, **kwargs, closures | print (no sep/end) | lambda |
| **Builtins** | 40+ functions | print (no sep/end), open (text only), set (no dedup) | bin/oct/hex, eval/exec, classmethod, slice |
| **String methods** | 17 working | format (stub) | ~24 more methods |
| **List methods** | 11 working (sort supports key=/reverse=) | | — |
| **Dict methods** | 7 working | pop (partial) | copy, fromkeys, popitem, setdefault |
| **Indexing & slicing** | list/dict/string, negative, slices, step | | slice assignment |
| **Comprehensions** | list, dict, set | | generator expressions |
| **Classes** | single/multiple inheritance, MRO, dunders, super, @property, @staticmethod | | @classmethod, metaclasses, __slots__ |
| **Exceptions** | try/except/finally, raise, custom exceptions, 10 built-in types | | try-else, exception chaining |
| **Decorators** | function/class/method, factories, stacked, @property, @staticmethod | | @classmethod, @prop.setter |
| **`with` statement** | __enter__/__exit__, as-target, exception suppression, nested | | contextlib.contextmanager |
| **Imports** | import mod, from mod import name/alias/*, .py files | | stdlib, packages, relative, circular |
| **Async** | — | — | All |
